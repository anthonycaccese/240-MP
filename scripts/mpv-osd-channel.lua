-- Channel OSD for 240-MP's virtual channels.
--
-- Draws a brief channel banner in the top right when the viewer changes
-- channel, the way a cable box does. Triggered over IPC with:
--
--     script-message 240mp-osd-channel "<number>" "<name>"
--
-- Lives in mpv rather than QML because during playback mpv owns the screen
-- through DRM — a QML overlay would be painted behind the video and never
-- seen. This is the same reason the volume bar in mpv-media-keys.lua is drawn
-- here.
--
-- Uses its own create_osd_overlay surface so it cannot clobber the navigation
-- menu (which owns the legacy set_osd_ass surface) or the volume bar. It sits
-- top right, where neither of those draw, so unlike the volume bar it does not
-- need to negotiate for the space.
--
-- How long it stays and where it sits are passed in with each banner rather
-- than configured here: 240-MP keeps those as module settings, and sending
-- them along means the script needs no notion of settings at all. An empty
-- number or name simply is not drawn, which is how the toggles work.

local BANNER_TIMEOUT = 1.5    -- seconds the banner stays on screen
local RETRY_INTERVAL = 0.1    -- how often to look for an OSD size
-- Fifteen seconds. Generous on purpose: a programme streamed from Plex over a
-- home network can take ten seconds to put its first frame up on a Pi, and
-- four seconds was short enough that every server-backed channel silently lost
-- its banner. Waiting costs one cheap timer callback per tick and the banner
-- still lands the moment the picture does, which is when it is wanted.
local RETRY_LIMIT    = 150

local overlay = mp.create_osd_overlay("ass-events")
local timer   = nil
local retry   = nil
local pending = nil

local function stop_retrying()
    if retry then retry:kill(); retry = nil end
    pending = nil
end

local function hide()
    if timer then timer:kill(); timer = nil end
    -- Also abandons a banner still waiting for an OSD size: without this a
    -- request to clear the screen would be undone a moment later.
    stop_retrying()
    overlay:remove()
end

-- Offered so the menu can clear the banner if it ever needs the whole screen.
mp.register_script_message("240mp-osd-channel-hide", hide)

-- ASS wants colours as &HBBGGRR&, which is the reverse of the #RRGGBB the app
-- hands around. Anything unparseable falls back to white, because a banner in
-- the wrong colour is a great deal better than no banner.
local function ass_colour(hex)
    local r, g, b = tostring(hex or ""):match("^#?(%x%x)(%x%x)(%x%x)$")
    if not r then return "&HFFFFFF&" end
    return "&H" .. b .. g .. r .. "&"
end

-- A banner that never goes away would sit over the picture for the rest of the
-- programme. The settings screen already keeps this between 0.5 and 10 seconds,
-- but config.json can be edited by hand, and this is the last place that can
-- still say no.
local MIN_SECONDS = 0.5
local MAX_SECONDS = 30

-- ASS text is escaped rather than trusted: a channel name is user-supplied and
-- a stray backslash or brace would otherwise be read as override tags and could
-- corrupt the rest of the banner.
local function ass_escape(s)
    s = tostring(s or "")
    s = s:gsub("\\", "\\\\")
    s = s:gsub("{", "\\{")
    s = s:gsub("}", "\\}")
    s = s:gsub("\n", " ")
    return s
end

-- Draws the banner, or returns false if mpv cannot say how big the screen is
-- yet. Separated from show() so the caller can wait rather than give up.
-- A font name goes inside an override block rather than into the text, so
-- escaping it the way the text is escaped would not help — a '}' there ends the
-- block early whether or not it is preceded by a backslash. It is dropped
-- instead. Nothing the app passes contains these; this is here so that stays
-- true if the font ever becomes something a viewer can set.
local function ass_font(s)
    return (tostring(s or ""):gsub("[{}\\\n]", ""))
end

local function draw(opts)
    local number, name = opts.number, opts.name
    local ww, wh = mp.get_osd_size()
    if not ww or ww == 0 or wh == 0 then return false end

    -- Declare the coordinate space the positions below are written in.
    --
    -- Without this the overlay keeps its default (res_y = 720, res_x derived
    -- from the aspect), and coordinates computed from the real OSD size are
    -- read as if they were in that other space — which put the banner in the
    -- middle of the screen at the wrong size rather than in the corner.
    overlay.res_x = ww
    overlay.res_y = wh

    -- The nudges arrive as a fraction of the screen so they mean the same
    -- thing whatever the output is: a CRT over composite needs a very
    -- different inset from an HDMI panel, and the viewer is the one who can
    -- see it.
    local margin_x = math.floor(ww * 0.06)   -- clear of CRT overscan
    local margin_y = math.floor(wh * 0.06)
    local x = math.floor(ww - margin_x + (opts.offset_x or 0) * ww)
    local y = math.floor(margin_y + (opts.offset_y or 0) * wh)

    local num_fs   = math.floor(wh * 0.11)
    local name_fs  = math.floor(wh * 0.045)

    local num_text  = ass_escape(number)
    local name_text = ass_escape(name)

    -- The app's own font and colour, passed in with the banner. Without the
    -- font name libass picks a system sans, which is the one thing on screen
    -- that would not look like the rest of 240-MP.
    local face   = opts.font ~= "" and ("\\fn" .. ass_font(opts.font)) or ""
    local colour = ass_colour(opts.colour)

    -- \an9 anchors to the top right, so the banner grows leftward and stays put
    -- regardless of how long the channel name is.
    local parts = {}
    if num_text ~= "" then
        parts[#parts + 1] = string.format(
            "{\\an9\\pos(%d,%d)%s\\fs%d\\bord3\\shad1\\1c%s\\3c&H000000&}%s",
            x, y, face, num_fs, colour, num_text)
        -- The name tucks under the number, or takes its place when the number
        -- is switched off rather than leaving a gap where it would have been.
        y = y + num_fs + math.floor(wh * 0.01)
    end

    if name_text ~= "" then
        parts[#parts + 1] = string.format(
            "{\\an9\\pos(%d,%d)%s\\fs%d\\bord2\\shad1\\1c%s\\3c&H000000&}%s",
            x, y, face, name_fs, colour, name_text)
    end

    if #parts == 0 then hide(); return true end

    overlay.data = table.concat(parts, "\n")
    overlay:update()
    mp.msg.info(string.format("channel banner drawn at %dx%d", ww, wh))

    if timer then timer:kill() end
    timer = mp.add_timeout(opts.seconds or BANNER_TIMEOUT, hide)
    return true
end

local function show(number, name, seconds, offset_x, offset_y, font, colour)
    stop_retrying()
    local opts = {
        number   = number or "",
        name     = name or "",
        seconds  = math.max(MIN_SECONDS,
                            math.min(MAX_SECONDS, tonumber(seconds) or BANNER_TIMEOUT)),
        offset_x = tonumber(offset_x) or 0,
        offset_y = tonumber(offset_y) or 0,
        font     = font or "",
        colour   = colour or "",
    }
    if draw(opts) then return end

    -- The banner is asked for the moment the IPC socket connects, which is the
    -- earliest 240-MP can ask — and that is before the first frame is decoded,
    -- so mpv does not yet know how big the OSD is. Dropping it there is why the
    -- banner never appeared on a channel change: every single one arrived too
    -- early. Wait for a size instead, and only then draw.
    local tries = 0
    pending = opts
    retry = mp.add_periodic_timer(RETRY_INTERVAL, function()
        tries = tries + 1
        if pending and draw(pending) then
            stop_retrying()
        elseif tries >= RETRY_LIMIT then
            mp.msg.warn("channel banner dropped: no OSD size after "
                        .. tostring(RETRY_INTERVAL * RETRY_LIMIT) .. "s")
            stop_retrying()
        end
    end)
end

mp.register_script_message("240mp-osd-channel", show)
