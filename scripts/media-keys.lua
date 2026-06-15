-- HID media-key handling for 240-MP.
--
-- Loaded for every mpv launch (all modules) so keyboard media keys work anytime
-- mpv is playing. Binds the canonical mpv key names — which both real HID media
-- keys (macOS, where mpv holds the keyboard) and synthetic `keypress` events
-- forwarded from InputManager over IPC (RPi/EGLFS) resolve to.
--
-- Volume keys also draw a retro "VOLUME" bar in the selected theme's primary
-- colour. The bar uses its own create_osd_overlay surface so it never clobbers
-- the navigation menu in mpv-osc.lua, which owns the legacy set_osd_ass surface.

local assdraw = require 'mp.assdraw'

local VOLUME_STEP    = 5     -- percentage points per Volume +/- press
local SEEK_FORWARD   = 30    -- Fast Forward jump, seconds
local SEEK_BACK      = 10    -- Rewind jump, seconds
local BAR_TIMEOUT    = 1.5   -- seconds the volume bar stays on screen
local BAR_TICKS      = 20    -- one tick per VOLUME_STEP across the 0–100 range

-- ASS colours are &HBBGGRR& (byte-reversed from #RRGGBB). Default to white when
-- the host app does not pass a primary colour.
local function ass_colour(hex)
    hex = (hex or ""):gsub("^#", "")
    if #hex ~= 6 then return "&HFFFFFF&" end
    return "&H" .. hex:sub(5, 6) .. hex:sub(3, 4) .. hex:sub(1, 2) .. "&"
end

local C_PRIMARY = ass_colour(mp.get_opt("primary-color"))
local A_OPAQUE  = "&H00&"
local A_DIM     = "&HB0&"   -- ~30% opacity for the unfilled "dash" ticks

local bar_overlay = mp.create_osd_overlay("ass-events")
local bar_timer   = nil

local function hide_bar()
    if bar_timer then bar_timer:kill(); bar_timer = nil end
    bar_overlay:remove()
end

-- The navigation menu (mpv-osc.lua) broadcasts this when it opens; the volume
-- bar and the menu share the same spot, so we stand down.
mp.register_script_message("240mp-osd-volume-hide", hide_bar)

-- Draw a filled rectangle (no border) at an absolute position.
local function draw_rect(ass, x, y, w, h, colour, alpha)
    ass:new_event()
    ass:pos(x, y)
    ass:append(string.format("{\\bord0\\shad0\\1c%s\\1a%s}", colour, alpha))
    ass:draw_start()
    ass:rect_cw(0, 0, w, h)
    ass:draw_stop()
end

-- Draw a text label in VCR OSD Mono.
local function draw_text(ass, x, y, anchor, text, fs, colour)
    ass:new_event()
    ass:append(string.format(
        "{\\an%d\\pos(%d,%d)\\fnVCR OSD Mono\\fs%d\\1c%s\\1a%s\\shad0\\bord0}%s",
        anchor, x, y, fs, colour, A_OPAQUE, text))
end

local function show_volume_bar()
    -- Tell the navigation menu (mpv-osc.lua) to stand down — the two OSDs share
    -- the same spot and are mutually exclusive.
    mp.commandv("script-message", "240mp-osd-menu-hide")

    local ww, wh = mp.get_osd_size()
    if ww == 0 or wh == 0 then return end

    local volume = mp.get_property_number("volume", 0) or 0
    local filled = math.floor(volume / VOLUME_STEP + 0.5)
    if filled < 0 then filled = 0 elseif filled > BAR_TICKS then filled = BAR_TICKS end

    -- Geometry mirrors the seek bar in mpv-osc.lua so the volume bar lands in the
    -- same place at the same size: label on the time-text row, bar on the seek row.
    local fs       = math.floor(wh * 0.0333333)
    local lm       = math.floor(ww * 0.12)
    local rm       = math.floor(ww * 0.88)
    local bar_w    = rm - lm
    local bar_h    = math.floor(fs * 2)
    local row1_y   = math.floor(wh * 0.7979166)   -- label row
    local bar_y    = math.floor(wh * 0.8333333)   -- bar row (nav menu's button row)
    local label_fs = fs * 3                        -- "VOLUME" reads large per the design

    -- A filled tick is a full-height vertical bar; an empty tick is a short dash.
    -- Both occupy the same slot so the row width stays constant as volume changes.
    local slot_w = bar_w / BAR_TICKS
    local gap    = math.max(1, math.floor(slot_w * 0.35))
    local tick_w = math.max(1, math.floor(slot_w - gap))
    local tick_h = bar_h
    local dash_h = math.max(2, math.floor(tick_h * 0.15))

    local ass = assdraw.ass_new()
    -- Bottom-left anchor (\an1) so the 3x label grows upward off the bar row and
    -- never overlaps the ticks below it.
    draw_text(ass, lm, row1_y, 1, "VOLUME", label_fs, C_PRIMARY)

    local x = lm
    for i = 1, BAR_TICKS do
        if i <= filled then
            draw_rect(ass, math.floor(x), bar_y, tick_w, tick_h, C_PRIMARY, A_OPAQUE)
        else
            -- Dash centred vertically within the tick's slot.
            draw_rect(ass, math.floor(x), bar_y + math.floor((tick_h - dash_h) / 2),
                      tick_w, dash_h, C_PRIMARY, A_DIM)
        end
        x = x + slot_w
    end

    bar_overlay.res_x = ww
    bar_overlay.res_y = wh
    bar_overlay.data  = ass.text
    bar_overlay:update()

    if bar_timer then bar_timer:kill() end
    bar_timer = mp.add_timeout(BAR_TIMEOUT, hide_bar)
end

local function change_volume(delta)
    mp.command("no-osd add volume " .. delta)
    show_volume_bar()
end

mp.add_forced_key_binding("VOLUME_UP",   "mk-vol-up",   function() change_volume(VOLUME_STEP)  end, {repeatable = true})
mp.add_forced_key_binding("VOLUME_DOWN", "mk-vol-down", function() change_volume(-VOLUME_STEP) end, {repeatable = true})
mp.add_forced_key_binding("MUTE",        "mk-mute",     function() mp.command("no-osd cycle mute"); show_volume_bar() end)

mp.add_forced_key_binding("PLAYPAUSE", "mk-playpause", function() mp.command("cycle pause") end)
mp.add_forced_key_binding("STOP",      "mk-stop",      function() mp.command("quit") end)

mp.add_forced_key_binding("FORWARD", "mk-forward", function() mp.command("no-osd seek " .. SEEK_FORWARD) end)
mp.add_forced_key_binding("REWIND",  "mk-rewind",  function() mp.command("no-osd seek -" .. SEEK_BACK) end)

mp.add_forced_key_binding("NEXT", "mk-next", function() mp.command("no-osd add chapter 1") end)
mp.add_forced_key_binding("PREV", "mk-prev", function() mp.command("no-osd add chapter -1") end)
