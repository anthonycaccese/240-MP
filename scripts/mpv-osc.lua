local assdraw = require 'mp.assdraw'
local mp_utils = require 'mp.utils'

-- Optional map of external sub-file URL -> friendly track name, written by the app
-- so the OSC can show a real subtitle name instead of mpv's URL-derived title
-- (Jellyfin sidecars are served as "Stream.srt?api_key=..."). Absent for most plays.
local subinfo = {}
do
    local path = mp.get_opt("subinfo-file")
    if path then
        local f = io.open(path, "r")
        if f then
            local parsed = mp_utils.parse_json(f:read("*a") or "")
            f:close()
            if type(parsed) == "table" then subinfo = parsed end
        end
    end
end

local menu_visible = false
local focus_row = 0  -- 0: Seek Bar, 1: Buttons
local focus_btn = 1  -- index into visible left buttons + STOP; varies with track availability
local update_timer = nil
local idle_timer = nil
local skip_active = false

local SEEK_SECONDS = 10
local MENU_TIMEOUT = 5

-- Colors (ABGR format for ASS)
local C_WHITE = "&HFFFFFF&"
local C_BLACK = "&H000000&"
local A_OPAQUE = "&H00&"
local A_TRANS  = "&HFF&"
local A_DIM    = "&H99&"  -- 40% opacity for unfocused seek fill

local function get_audio_str()
    local id = mp.get_property_number("current-tracks/audio/id", 0)
    if id == 0 then return "(NONE)" end
    local title    = (mp.get_property("current-tracks/audio/title", "") or ""):upper()
    local lang     = (mp.get_property("current-tracks/audio/lang",  "") or ""):upper()
    local codec    = (mp.get_property("current-tracks/audio/codec", "") or ""):upper()
    local channels = mp.get_property_number("current-tracks/audio/audio-channels", 0)
    local rate     = mp.get_property_number("current-tracks/audio/demux-samplerate", 0)
    local parts = {}
    if title    ~= "" then parts[#parts+1] = title end
    if lang     ~= "" then parts[#parts+1] = lang  end
    if codec    ~= "" then parts[#parts+1] = codec end
    if channels  > 0  then parts[#parts+1] = channels .. "CH" end
    if rate      > 0  then parts[#parts+1] = rate .. " HZ" end
    return table.concat(parts, " ")
end

local function get_sub_str()
    local id = mp.get_property_number("current-tracks/sub/id", 0)
    if id == 0 then return "(NONE)" end
    -- External sidecar with an app-provided friendly name (e.g. Jellyfin): use it
    -- instead of mpv's URL-derived title.
    local ext = mp.get_property("current-tracks/sub/external-filename", "") or ""
    if ext ~= "" and subinfo[ext] and subinfo[ext] ~= "" then
        return tostring(subinfo[ext]):upper()
    end
    local title = (mp.get_property("current-tracks/sub/title", "") or ""):upper()
    local lang  = (mp.get_property("current-tracks/sub/lang",  "") or ""):upper()
    local codec = (mp.get_property("current-tracks/sub/codec", "") or ""):upper()
    local parts = {}
    if title ~= "" then parts[#parts+1] = title end
    if lang  ~= "" then parts[#parts+1] = lang  end
    if codec ~= "" then parts[#parts+1] = codec end
    return table.concat(parts, " ")
end

local btn_actions = {
    function() mp.command("no-osd cycle audio") end,
    function() mp.command("no-osd cycle sub") end,
    function() mp.command("no-osd cycle-values panscan 0 1") end,
    function() mp.command("quit") end,
    function() mp.command("playlist-prev") end,
    function() mp.command("playlist-next") end,
}

local function has_subtitle_tracks()
    local tracks = mp.get_property_native("track-list", {})
    for _, t in ipairs(tracks) do
        if t.type == "sub" then return true end
    end
    return false
end

local function has_playlist()
    return (mp.get_property_number("playlist-count", 1) or 1) > 1
end

local function build_left_btns(has_sub, has_pl, bar_w)
    local btns = {}
    if skip_active then
        btns[#btns + 1] = {label="SKIP", width=math.floor(bar_w * 0.090625), action=function()
            mp.commandv("script-message", "skip-segment")
        end}
    end
    btns[#btns + 1] = {label="AUDIO", width=math.floor(bar_w * 0.109375), action=btn_actions[1]}
    if has_sub then
        table.insert(btns, {label="SUBTITLE", width=math.floor(bar_w * 0.15625), action=btn_actions[2]})
    end
    table.insert(btns, {label="CROP", width=math.floor(bar_w * 0.090625), action=btn_actions[3]})
    if has_pl then
        table.insert(btns, {label="<", width=math.floor(bar_w * 0.055), action=btn_actions[5]})
        table.insert(btns, {label=">", width=math.floor(bar_w * 0.055), action=btn_actions[6]})
    end
    return btns
end

local transcode_offset = tonumber(mp.get_opt("transcode-offset") or "0") or 0

-- Latch duration on first valid read; used to detect PTS base shifts during HLS seeking
local stable_duration = nil
mp.observe_property("duration", "number", function(_, value)
    if value and value > 0 and not stable_duration then
        stable_duration = value
    end
end)

local function format_time(seconds)
    if not seconds or seconds < 0 then seconds = 0 end
    local h = math.floor(seconds / 3600)
    local m = math.floor((seconds % 3600) / 60)
    local s = math.floor(seconds % 60)
    if h > 0 then
        return string.format("%d:%02d:%02d", h, m, s)
    else
        return string.format("%d:%02d", m, s)
    end
end

-- Draw a filled rectangle with an optional border.
-- Uses ass:pos() (no \an tag) to match mpv's expected drawing coordinate origin.
local function draw_rect(ass, x, y, w, h, fc, fa, bs, bc)
    ass:new_event()
    ass:pos(x, y)
    ass:append(string.format(
        "{\\bord%d\\3c%s\\3a&H00&\\1c%s\\1a%s\\shad0}",
        bs, bc, fc, fa))
    ass:draw_start()
    ass:rect_cw(0, 0, w, h)
    ass:draw_stop()
end

-- Draw a text label using VCR OSD Mono.
local function draw_text(ass, x, y, anchor, text, fs, fc, fa)
    ass:new_event()
    ass:append(string.format(
        "{\\an%d\\pos(%d,%d)\\fnVCR OSD Mono\\fs%d\\1c%s\\1a%s\\shad0\\bord0}%s",
        anchor, x, y, fs, fc, fa, text))
end

local function draw_menu()
    local ass = assdraw.ass_new()
    local ww, wh = mp.get_osd_size()
    if ww == 0 or wh == 0 then return end

    -- Layout constants
    local fs      = math.floor(wh * 0.0333333)   -- font size
    local lm      = math.floor(ww * 0.12)    -- left margin
    local rm      = math.floor(ww * 0.88)    -- right margin
    local bar_w   = rm - lm
    local border  = 2

    -- Heights derived from fs so they scale consistently with the font
    local bar_h   = math.floor(fs * 2)
    local btn_h   = math.floor(fs * 1.5)
    local btn_gap = math.floor(bar_w * 0.025)

    -- Row y-positions
    local row1_y  = math.floor(wh * 0.7083333)
    local bar_y   = math.floor(wh * 0.74375)
    local btn_y   = math.floor(wh * 0.8333333)

    local has_sub    = has_subtitle_tracks()
    local stop_w     = math.floor(bar_w * 0.090625)
    local left_btns  = build_left_btns(has_sub, has_playlist(), bar_w)

    -- ── Track info (top-left) ─────────────────────────────────────
    local info_fs  = math.floor(fs * 1)
    local info_lh  = math.floor(info_fs * 1.5)
    local info_y   = math.floor(wh * 0.125)

    draw_text(ass, lm, info_y, 4, "AUDIO: " .. get_audio_str(), info_fs, C_WHITE, A_OPAQUE)
    if has_sub then
        draw_text(ass, lm, info_y + info_lh, 4, "SUBTITLE: " .. get_sub_str(),  info_fs, C_WHITE, A_OPAQUE)
    end

    -- ── Row 1: Time text ──────────────────────────────────────────
    local total    = stable_duration or (mp.get_property_number("duration", 0) or 0)
    local time_pos = math.min(math.max(0, (mp.get_property_number("time-pos", 0) or 0) + transcode_offset), total)
    local percent  = (total > 0) and math.min(100, math.max(0, time_pos / total * 100)) or 0

    draw_text(ass, lm, row1_y, 4, format_time(time_pos), fs, C_WHITE, A_OPAQUE)
    draw_text(ass, rm, row1_y, 6, format_time(total),    fs, C_WHITE, A_OPAQUE)

    -- ── Row 2: Seek bar ───────────────────────────────────────────
    local pad   = 2
    local inset = border + pad

    -- Transparent box with white border
    draw_rect(ass, lm, bar_y, bar_w, bar_h, C_BLACK, A_TRANS, border, C_WHITE)

    -- Progress fill (full opacity when focused, 40% when not)
    local inner_w    = bar_w - 2 * inset
    local fill_w     = math.max(0, math.floor(inner_w * (percent / 100)))
    local fill_alpha = (focus_row == 0) and A_OPAQUE or A_DIM
    if fill_w > 0 then
        draw_rect(ass, lm + inset, bar_y + inset, fill_w, bar_h - 2 * inset,
                  C_WHITE, fill_alpha, 0, C_WHITE)
    end

    -- ── Row 3: Buttons ────────────────────────────────────────────
    -- Left group: AUDIO, [SUBTITLE], CROP
    local stop_idx = #left_btns + 1
    local bx = lm
    for i, btn in ipairs(left_btns) do
        local sel    = (focus_row == 1 and focus_btn == i)
        local fill_c = sel and C_WHITE or C_BLACK
        local fill_a = sel and A_OPAQUE or A_TRANS
        local text_c = sel and C_BLACK  or C_WHITE

        draw_rect(ass, bx, btn_y, btn.width, btn_h, fill_c, fill_a, border, C_WHITE)
        draw_text(ass, bx + btn.width / 2, btn_y + btn_h / 2, 5,
                  btn.label, fs, text_c, A_OPAQUE)
        bx = bx + btn.width + btn_gap
    end

    -- Right: STOP
    local stop_x = rm - stop_w
    local sel    = (focus_row == 1 and focus_btn == stop_idx)
    local fill_c = sel and C_WHITE or C_BLACK
    local fill_a = sel and A_OPAQUE or A_TRANS
    local text_c = sel and C_BLACK  or C_WHITE

    draw_rect(ass, stop_x, btn_y, stop_w, btn_h, fill_c, fill_a, border, C_WHITE)
    draw_text(ass, stop_x + stop_w / 2, btn_y + btn_h / 2, 5,
              "STOP", fs, text_c, A_OPAQUE)

    mp.set_osd_ass(ww, wh, ass.text)
end

local function reset_idle_timer()
    if idle_timer then idle_timer:kill() end
    idle_timer = mp.add_timeout(MENU_TIMEOUT, function()
        if menu_visible then
            menu_visible = false
            mp.set_osd_ass(0, 0, "")
            if update_timer then update_timer:stop() end
            mp.remove_key_binding("menu-up")
            mp.remove_key_binding("menu-down")
            mp.remove_key_binding("menu-left")
            mp.remove_key_binding("menu-right")
            mp.remove_key_binding("menu-enter")
            mp.remove_key_binding("menu-esc")
            mp.remove_key_binding("menu-bs")
        end
    end)
end

local function update_nav(action)
    reset_idle_timer()

    if action == "up" then
        focus_row = 0
    elseif action == "down" then
        focus_row = 1
    elseif action == "left" then
        if focus_row == 0 then
            mp.command("seek -" .. SEEK_SECONDS)
        else
            local has_sub = has_subtitle_tracks()
            local has_pl  = has_playlist()
            local ww, _   = mp.get_osd_size()
            local bar_w   = math.floor(ww * 0.88) - math.floor(ww * 0.12)
            local total   = #build_left_btns(has_sub, has_pl, bar_w) + 1
            focus_btn = focus_btn > 1 and focus_btn - 1 or total
        end
    elseif action == "right" then
        if focus_row == 0 then
            mp.command("seek " .. SEEK_SECONDS)
        else
            local has_sub = has_subtitle_tracks()
            local has_pl  = has_playlist()
            local ww, _   = mp.get_osd_size()
            local bar_w   = math.floor(ww * 0.88) - math.floor(ww * 0.12)
            local total   = #build_left_btns(has_sub, has_pl, bar_w) + 1
            focus_btn = focus_btn < total and focus_btn + 1 or 1
        end
    elseif action == "enter" and focus_row == 1 then
        local has_sub   = has_subtitle_tracks()
        local has_pl    = has_playlist()
        local ww, wh    = mp.get_osd_size()
        local bar_w     = math.floor(ww * 0.88) - math.floor(ww * 0.12)
        local btns      = build_left_btns(has_sub, has_pl, bar_w)
        local total     = #btns + 1
        local clamped   = math.min(focus_btn, total)
        if clamped <= #btns then
            btns[clamped].action()
        else
            btn_actions[4]()
        end
    end

    draw_menu()
end

local function toggle_menu()
    if menu_visible then
        menu_visible = false
        mp.set_osd_ass(0, 0, "")
        if update_timer then update_timer:stop() end
        if idle_timer   then idle_timer:kill()   end
        mp.remove_key_binding("menu-up")
        mp.remove_key_binding("menu-down")
        mp.remove_key_binding("menu-left")
        mp.remove_key_binding("menu-right")
        mp.remove_key_binding("menu-enter")
        mp.remove_key_binding("menu-esc")
        mp.remove_key_binding("menu-bs")
    else
        -- Tell the volume bar (media-keys.lua) to stand down — the two OSDs
        -- share the same spot and are mutually exclusive.
        mp.commandv("script-message", "240mp-osd-volume-hide")
        menu_visible = true
        focus_row    = 1
        draw_menu()
        update_timer = mp.add_periodic_timer(0.5, draw_menu)
        reset_idle_timer()

        mp.add_forced_key_binding("UP",    "menu-up",    function() update_nav("up")    end)
        mp.add_forced_key_binding("DOWN",  "menu-down",  function() update_nav("down")  end)
        mp.add_forced_key_binding("LEFT",  "menu-left",  function() update_nav("left")  end)
        mp.add_forced_key_binding("RIGHT", "menu-right", function() update_nav("right") end)
        mp.add_forced_key_binding("ENTER", "menu-enter", function() update_nav("enter") end)
        mp.add_forced_key_binding("ESC",   "menu-esc",   toggle_menu)
        mp.add_forced_key_binding("BS",    "menu-bs",    toggle_menu)
    end
end

-- The volume bar (media-keys.lua) broadcasts this when it appears; close the
-- menu so the two OSDs never overlap. toggle_menu() runs the full teardown.
mp.register_script_message("240mp-osd-menu-hide", function()
    if menu_visible then toggle_menu() end
end)

-- media-keys.lua broadcasts this on seek / chapter changes so the nav menu
-- pops up to show the new position. Open it if closed; otherwise just redraw
-- and restart the auto-hide timer.
mp.register_script_message("240mp-osd-menu-show", function()
    if menu_visible then
        reset_idle_timer()
        draw_menu()
    else
        toggle_menu()
    end
end)

-- Forced bindings so UP/DOWN take priority over mpv's default seek bindings
-- on desktop (macOS/Linux with native keyboard input).
mp.add_forced_key_binding("UP",   "open_menu_up",   toggle_menu)
mp.add_forced_key_binding("DOWN", "open_menu_down", toggle_menu)

-- ESC / BS quit when the menu is not visible. When the menu opens it adds
-- forced bindings for these keys that take priority automatically; when it
-- closes those forced bindings are removed and these become active again.
mp.add_key_binding("ESC", "bg-esc", function() mp.command("quit") end)
mp.add_key_binding("BS",  "bg-bs",  function() mp.command("quit") end)

mp.register_script_message("skip-overlay-state", function(state)
    skip_active = (state == "1")
    -- Land focus on SKIP (first button) so ENTER skips immediately —
    -- focus_btn otherwise persists from the last menu interaction.
    if skip_active then focus_btn = 1 end
end)
