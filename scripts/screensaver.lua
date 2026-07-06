-- Screen saver for 240-MP: bounces "240-MP" text across a solid black
-- background when the video has been paused longer than the configured timeout.
--
-- Loaded by --script= only when screensaver_timeout != "OFF".
-- Uses the same ASS-overlay technique as media-keys.lua's volume bar.
-- Dismiss keys are consumed, matching the app-shell overlay: the first press
-- only wakes the screen saver, the next press acts normally.

local assdraw = require("mp.assdraw")

-- Raw script-opts key (same convention as mpv-osc.lua's transcode-offset);
-- mp.options.read_options would instead expect a "screensaver-" prefix.
local timeout_sec = tonumber(mp.get_opt("screensaver_timeout") or "60") or 60
local SPEED = 2

-- ─── state ───────────────────────────────────────────────────────────────────
local ss_active   = false
local paused_sec  = 0
local x, y   = 20, 20
local vx, vy = SPEED, SPEED
-- Bounce box for "240-MP" at \fs108 (overlay units == OSD pixels)
local TEXT_W  = 400
local TEXT_H  = 120

-- ─── forward declarations ─────────────────────────────────────────────────────
local activate, dismiss, anim_timer

-- ─── ASS overlay ──────────────────────────────────────────────────────────────
local overlay = mp.create_osd_overlay("ass-events")

-- ─── draw full frame ─────────────────────────────────────────────────────────
local function draw_frame(w, h, tx, ty)
    local a = assdraw.ass_new()
    -- Solid black background covering the entire OSD area
    a:new_event()
    a:pos(0, 0)
    a:append(string.format(
        "{\\bord0\\shad0\\1c&H000000&\\1a&H00&\\p1}m 0 0 l %d 0 l %d %d l 0 %d{\\p0}",
        w, w, h, h
    ))
    -- Bouncing text on top
    a:new_event()
    a:append(string.format(
        "{\\an7\\pos(%d,%d)\\fs108\\b1\\c&HFFFFFF&}240-MP{\\b0}",
        math.floor(tx), math.floor(ty)
    ))
    overlay.res_x = w
    overlay.res_y = h
    overlay.data  = a.text
    overlay:update()
end

-- ─── key bindings ─────────────────────────────────────────────────────────────
local DISMISS_KEYS = {
    "SPACE", "ENTER", "KP_ENTER", "ESC",
    "LEFT", "RIGHT", "UP", "DOWN", "PGUP", "PGDWN",
    "HOME", "END",
    "MBTN_LEFT", "MBTN_RIGHT",
}

activate = function()
    if ss_active then return end
    ss_active = true

    local ww, wh = mp.get_osd_size()
    if ww == 0 then ww = 640 end
    if wh == 0 then wh = 480 end

    x = math.floor(math.random() * math.max(1, math.floor(ww / 2)))
    y = math.floor(math.random() * math.max(1, math.floor(wh / 2)))
    vx = SPEED
    vy = SPEED

    for _, k in ipairs(DISMISS_KEYS) do
        mp.add_forced_key_binding(k, "ss-dismiss-" .. k, dismiss)
    end

    draw_frame(ww, wh, x, y)
    anim_timer:resume()
end

dismiss = function()
    if not ss_active then return end
    ss_active = false
    paused_sec = 0

    overlay:remove()
    anim_timer:kill()

    for _, k in ipairs(DISMISS_KEYS) do
        pcall(mp.remove_key_binding, "ss-dismiss-" .. k)
    end
end

-- ─── 1-second tick: count up while paused ────────────────────────────────────
local pause_monitor = mp.add_periodic_timer(1.0, function()
    if ss_active then return end
    local is_paused = mp.get_property_native("pause")
    if is_paused then
        paused_sec = paused_sec + 1
        if paused_sec >= timeout_sec then
            activate()
        end
    else
        paused_sec = 0
    end
end)

-- ─── ~60 fps animation ───────────────────────────────────────────────────────
anim_timer = mp.add_periodic_timer(0.016, function()
    if not ss_active then return end

    local ww, wh = mp.get_osd_size()
    if ww == 0 or wh == 0 then return end

    x = x + vx
    y = y + vy

    if x + TEXT_W > ww then
        x = ww - TEXT_W
        vx = -math.abs(vx)
    elseif x < 0 then
        x = 0
        vx = math.abs(vx)
    end

    if y + TEXT_H > wh then
        y = wh - TEXT_H
        vy = -math.abs(vy)
    elseif y < 0 then
        y = 0
        vy = math.abs(vy)
    end

    draw_frame(ww, wh, x, y)
end)
anim_timer:kill()
