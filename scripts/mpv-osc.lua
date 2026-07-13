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

local function format_decimal(value, decimals)
    if not value or value <= 0 then return "" end
    local text = string.format("%." .. decimals .. "f", value)
    text = text:gsub("0+$", ""):gsub("%.$", "")
    return text
end

local function video_codec_name(codec)
    codec = (codec or ""):lower()

    local names = {
        h264       = "H.264",
        hevc       = "HEVC",
        h265       = "HEVC",
        mpeg2video = "MPEG-2",
        mpeg4      = "MPEG-4",
        vp8        = "VP8",
        vp9        = "VP9",
        av1        = "AV1",
        vc1        = "VC-1",
        theora     = "THEORA"
    }

    return names[codec] or codec:upper()
end

local function audio_codec_name(codec)
    codec = (codec or ""):lower()

    if codec:match("^pcm") then return "PCM" end

    local names = {
        aac      = "AAC",
        mp3      = "MP3",
        mp2      = "MP2",
        ac3      = "AC-3",
        eac3     = "E-AC-3",
        dts      = "DTS",
        truehd   = "TrueHD",
        flac     = "FLAC",
        alac     = "ALAC",
        opus     = "Opus",
        vorbis   = "Vorbis",
        wavpack  = "WAVPACK"
    }

    return names[codec] or codec:upper()
end

local function resolution_name(height, interlaced)
    if not height or height <= 0 then return "" end

    local label
    if height <= 240 then
        label = "240"
    elseif height <= 360 then
        label = "360"
    elseif height <= 480 then
        label = "480"
    elseif height <= 576 then
        label = "576"
    elseif height <= 720 then
        label = "720"
    elseif height <= 1080 then
        label = "1080"
    elseif height <= 1440 then
        label = "1440"
    elseif height <= 2160 then
        label = "2160"
    else
        label = tostring(math.floor(height + 0.5))
    end

    return label .. (interlaced and "i" or "p")
end

local function aspect_name(aspect)
    if not aspect or aspect <= 0 then return "" end

    if math.abs(aspect - (4 / 3)) < 0.035 then
        return "4:3"
    elseif math.abs(aspect - (16 / 9)) < 0.035 then
        return "16:9"
    elseif math.abs(aspect - 1.85) < 0.035 then
        return "1.85:1"
    elseif aspect >= 2.35 and aspect <= 2.43 then
        return "2.39:1"
    end

    return format_decimal(aspect, 2) .. ":1"
end

local function channel_name(channels)
    if not channels or channels <= 0 then
        return ""
    elseif channels == 1 then
        return "MONO"
    elseif channels == 2 then
        return "STEREO"
    else
        return "SURROUND"
    end
end

local function sample_rate_name(rate)
    if not rate or rate <= 0 then return "" end

    local khz = rate / 1000
    return format_decimal(khz, 1) .. " KHZ"
end

local function clean_media_filename(value)
    if not value or value == "" then return "" end

    value = value:gsub("^.*[/\\]", "")
    value = value:gsub("%.[^%.]+$", "")
    value = value:gsub("[._]+", " ")
    value = value:gsub("%s+", " ")
    value = value:gsub("^%s+", ""):gsub("%s+$", "")

    -- Eliminar descriptores comunes desde la resolución.
    value = value:gsub("%s+2160[Pp].*$", "")
    value = value:gsub("%s+1080[PpIi].*$", "")
    value = value:gsub("%s+720[Pp].*$", "")
    value = value:gsub("%s+576[PpIi].*$", "")
    value = value:gsub("%s+480[PpIi].*$", "")

    -- TITLE 1999 -> TITLE (1999)
    local base, year = value:match("^(.-)%s+(19%d%d)$")
    if not base then
        base, year = value:match("^(.-)%s+(20%d%d)$")
    end

    if base and year then
        value = base .. " (" .. year .. ")"
    end

    return value:upper()
end

local function get_display_title()
    local function is_technical(value)
        if not value or value == "" then
            return true
        end

        local lower = value:lower()

        if lower:match("^https?://")
                or lower:match("^ytdl://")
                or lower:find("x%-plex%-")
                or lower:find("client%-identifier")
                or lower:find("/library/")
                or lower:find("library/metadata")
                or lower:find("metadata/")
                or lower:find("%.mkv%?")
                or lower:find("%.mp4%?")
                or lower:find("%.avi%?")
                or lower:find("%.ts%?")
                or lower:find("%.m4v%?") then
            return true
        end

        if #value > 180 then
            return true
        end

        local slash_count = select(2, value:gsub("[/\\]", ""))
        local equals_count = select(2, value:gsub("=", ""))
        local amp_count = select(2, value:gsub("&", ""))

        if slash_count >= 2 or equals_count >= 2 or amp_count >= 3 then
            return true
        end

        return false
    end

    local embedded =
        mp.get_property("metadata/by-key/title", "") or ""

    if embedded == "" then
        embedded =
            mp.get_property("metadata/by-key/Title", "") or ""
    end

    if embedded ~= "" and not is_technical(embedded) then
        return embedded:upper()
    end

    local source_path = mp.get_property("path", "") or ""
    local media_title = mp.get_property("media-title", "") or ""

    if media_title ~= ""
            and media_title ~= source_path
            and not is_technical(media_title) then
        return media_title:upper()
    end

    if source_path ~= ""
            and not source_path:match("^https?://")
            and not source_path:match("^ytdl://")
            and not is_technical(source_path) then
        return clean_media_filename(source_path)
    end

    return ""
end

local function get_video_str()
    local id = mp.get_property_number("current-tracks/video/id", 0)
    if id == 0 then return "" end

    local codec = video_codec_name(
        mp.get_property("current-tracks/video/codec", "") or ""
    )

    local height = mp.get_property_number(
        "current-tracks/video/demux-h", 0
    )

    if height == 0 then
        height = mp.get_property_number("video-params/h", 0)
    end

    local interlaced_value =
        mp.get_property_native("video-params/interlaced", false)

    local interlaced =
        interlaced_value == true
        or interlaced_value == "yes"
        or interlaced_value == "true"

    local resolution = resolution_name(height, interlaced)

    -- FPS declarados por la pista, no FPS variables del renderizado.
    local fps = mp.get_property_number(
        "current-tracks/video/demux-fps", 0
    )

    local fps_text = ""
    if fps > 0 then
        if math.abs(fps - 23.976) < 0.08 then
            fps_text = "23.97 FPS"
        elseif math.abs(fps - 29.97) < 0.08 then
            fps_text = "29.97 FPS"
        elseif math.abs(fps - 59.94) < 0.08 then
            fps_text = "59.94 FPS"
        else
            fps_text = string.format("%d FPS", math.floor(fps + 0.5))
        end
    end

    local aspect = aspect_name(
        mp.get_property_number("video-params/aspect", 0)
    )

    local parts = {}
    if codec ~= ""      then parts[#parts + 1] = codec      end
    if resolution ~= "" then parts[#parts + 1] = resolution end
    if aspect ~= ""     then parts[#parts + 1] = aspect     end
    if fps_text ~= ""   then parts[#parts + 1] = fps_text   end

    return table.concat(parts, " · ")
end

local function useful_language(value)
    value = (value or ""):upper()
    value = value:gsub("^%s+", ""):gsub("%s+$", "")

    local aliases = {
        EN = "ENG",
        ENG = "ENG",
        ENGLISH = "ENG",

        ES = "SPA",
        SPA = "SPA",
        SPANISH = "SPA",
        ESPANOL = "SPA",
        ["ESPAÑOL"] = "SPA",
        LATINO = "SPA",

        FR = "FRA",
        FRA = "FRA",
        FRE = "FRA",
        FRENCH = "FRA",

        DE = "GER",
        DEU = "GER",
        GER = "GER",
        GERMAN = "GER",

        IT = "ITA",
        ITA = "ITA",

        PT = "POR",
        POR = "POR",

        JA = "JPN",
        JPN = "JPN",
        JAPANESE = "JPN"
    }

    if aliases[value] then
        return aliases[value]
    end

    local ignored = {
        AUDIO = true,
        LANGUAGE = true,
        LANG = true,
        TEXT = true,
        SUB = true,
        SUBS = true,
        SUBTITLE = true,
        SUBTITLES = true,
        STEREO = true,
        MONO = true,
        SURROUND = true,
        DEFAULT = true,
        FORCED = true,
        TRACK = true,
        UNKNOWN = true
    }

    if ignored[value] then
        return ""
    end

    if value:match("^[A-Z][A-Z][A-Z]$") then
        return value
    end

    return ""
end

local function track_position(track_type)
    local tracks = mp.get_property_native("track-list", {}) or {}
    local current_id = mp.get_property_number(
        "current-tracks/" .. track_type .. "/id", 0
    )

    if current_id == 0 then
        return ""
    end

    local total = 0
    local current = 0

    for _, track in ipairs(tracks) do
        if track.type == track_type then
            total = total + 1

            if track.id == current_id then
                current = total
            end
        end
    end

    if current == 0 or total == 0 then
        return ""
    end

    return string.format("TRACK %d/%d", current, total)
end

local function get_audio_str()
    local id = mp.get_property_number("current-tracks/audio/id", 0)
    if id == 0 then return "" end

    local codec = audio_codec_name(
        mp.get_property("current-tracks/audio/codec", "") or ""
    )

    local channels = channel_name(
        mp.get_property_number(
            "current-tracks/audio/audio-channels", 0
        )
    )

    local track_text = track_position("audio")

    local lang = useful_language(
        mp.get_property("current-tracks/audio/lang", "") or ""
    )

    if lang == "" then
        lang = useful_language(
            mp.get_property("current-tracks/audio/title", "") or ""
        )
    end

    local parts = {}

    if codec ~= "" then
        parts[#parts + 1] = codec
    end

    if channels ~= "" then
        parts[#parts + 1] = channels
    end

    if track_text ~= "" then
        parts[#parts + 1] = track_text
    end

    if lang ~= "" then
        parts[#parts + 1] = lang
    end

    return table.concat(parts, " · ")
end

local function get_sub_str()
    local id = mp.get_property_number("current-tracks/sub/id", 0)
    if id == 0 then return "" end

    local ext =
        mp.get_property(
            "current-tracks/sub/external-filename", ""
        ) or ""

    if ext ~= "" and subinfo[ext] and subinfo[ext] ~= "" then
        return tostring(subinfo[ext]):upper()
    end

    local codec = (
        mp.get_property("current-tracks/sub/codec", "") or ""
    ):upper()

    local title = (
        mp.get_property("current-tracks/sub/title", "") or ""
    ):upper()

    local lang = (
        mp.get_property("current-tracks/sub/lang", "") or ""
    ):upper()

    local track = title ~= "" and title or lang

    local parts = {}
    if codec ~= "" then parts[#parts + 1] = codec end
    if track ~= "" then parts[#parts + 1] = track end

    return table.concat(parts, " · ")
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

    local display_title = get_display_title()
    local video_info = get_video_str()
    local audio_info = get_audio_str()
    local sub_info = get_sub_str()

    local info_line_y = info_y

    if display_title ~= "" then
        draw_text(ass, lm, info_line_y, 4,
                  display_title,
                  info_fs, C_WHITE, A_OPAQUE)
        info_line_y = info_line_y + info_lh
    end

    if video_info ~= "" then
        draw_text(ass, lm, info_line_y, 4,
                  "VIDEO: " .. video_info,
                  info_fs, C_WHITE, A_OPAQUE)
        info_line_y = info_line_y + info_lh
    end

    if audio_info ~= "" then
        draw_text(ass, lm, info_line_y, 4,
                  "AUDIO: " .. audio_info,
                  info_fs, C_WHITE, A_OPAQUE)
        info_line_y = info_line_y + info_lh
    end

    if sub_info ~= "" then
        draw_text(ass, lm, info_line_y, 4,
                  "SUBTITLE: " .. sub_info,
                  info_fs, C_WHITE, A_OPAQUE)
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
