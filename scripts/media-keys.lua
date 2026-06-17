-- HID media-key handling for 240-MP.
--
-- Loaded for every mpv launch (all modules) so keyboard media keys work anytime
-- mpv is playing. Binds the canonical mpv key names — which both real HID media
-- keys (macOS, where mpv holds the keyboard) and synthetic `keypress` events
-- forwarded from InputManager over IPC (RPi/EGLFS) resolve to.
--
-- Volume keys also draw a retro "VOLUME" bar. The bar uses its own
-- create_osd_overlay surface so it never clobbers the navigation menu in
-- mpv-osc.lua, which owns the legacy set_osd_ass surface.

local assdraw = require 'mp.assdraw'

local VOLUME_STEP    = 5     -- percentage points per Volume +/- press
local SEEK_FORWARD   = 30    -- Fast Forward jump, seconds
local SEEK_BACK      = 10    -- Rewind jump, seconds
local BAR_TIMEOUT    = 1.5   -- seconds the volume bar stays on screen

local C_WHITE   = "&HFFFFFF&"
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

    -- One tick per VOLUME_STEP across the full 0..volume-max range, so a config
    -- that raises volume-max above 100 yields more (and thus thinner) ticks.
    local volume   = mp.get_property_number("volume", 0) or 0
    local vol_max  = mp.get_property_number("volume-max", 100) or 100
    local ticks    = math.max(1, math.floor(vol_max / VOLUME_STEP + 0.5))
    local filled   = math.max(0, math.min(math.floor(volume / VOLUME_STEP + 0.5), ticks))

    local fs       = math.floor(wh * 0.0333333)
    local lm       = math.floor(ww * 0.12)
    local bar_w    = math.floor(ww * 0.88) - lm
    local bar_h    = math.floor(fs * 2)
    local row1_y   = math.floor(wh * 0.7979166)   -- label row
    local bar_y    = math.floor(wh * 0.8333333)   -- bar row (nav menu's button row)
    local label_fs = fs * 3                        -- "VOLUME" reads large per the design

    -- A filled tick is a full-height vertical bar; an empty tick is a short dash.
    -- Both occupy the same slot so the row width stays constant as volume changes.
    local slot_w = bar_w / ticks
    local gap    = math.max(1, math.floor(slot_w * 0.35))
    local tick_w = math.max(1, math.floor(slot_w - gap))
    local dash_h = math.max(2, math.floor(bar_h * 0.15))

    local ass = assdraw.ass_new()
    -- Bottom-left anchor (\an1) so the 3x label grows upward off the bar row and
    -- never overlaps the ticks below it. Label reflects mute state so a mute
    -- toggle (which leaves the volume ticks unchanged) is still legible.
    local label = mp.get_property_bool("mute", false) and "MUTE" or "VOLUME"
    draw_text(ass, lm, row1_y, 1, label, label_fs, C_WHITE)

    local x = lm
    for i = 1, ticks do
        if i <= filled then
            draw_rect(ass, math.floor(x), bar_y, tick_w, bar_h, C_WHITE, A_OPAQUE)
        else
            -- Dash centred vertically within the tick's slot.
            draw_rect(ass, math.floor(x), bar_y + math.floor((bar_h - dash_h) / 2),
                      tick_w, dash_h, C_WHITE, A_DIM)
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

-- Run a seek/chapter command, then open the nav menu (mpv-osc.lua) so the new
-- position is shown.
local function seek_with_menu(command)
    mp.command(command)
    mp.commandv("script-message", "240mp-osd-menu-show")
end

mp.add_forced_key_binding("VOLUME_UP",   "mk-vol-up",   function() change_volume(VOLUME_STEP)  end, {repeatable = true})
mp.add_forced_key_binding("VOLUME_DOWN", "mk-vol-down", function() change_volume(-VOLUME_STEP) end, {repeatable = true})
mp.add_forced_key_binding("MUTE",        "mk-mute",     function() mp.command("no-osd cycle mute"); show_volume_bar() end)

mp.add_forced_key_binding("PLAYPAUSE", "mk-playpause", function() mp.command("cycle pause") end)
mp.add_forced_key_binding("STOP",      "mk-stop",      function() mp.command("quit") end)

mp.add_forced_key_binding("FORWARD", "mk-forward", function() seek_with_menu("no-osd seek " .. SEEK_FORWARD) end)
mp.add_forced_key_binding("REWIND",  "mk-rewind",  function() seek_with_menu("no-osd seek -" .. SEEK_BACK) end)

mp.add_forced_key_binding("NEXT", "mk-next", function() seek_with_menu("no-osd add chapter 1") end)
mp.add_forced_key_binding("PREV", "mk-prev", function() seek_with_menu("no-osd add chapter -1") end)
