-- vo=drm/gpu on KMS only repaints the primary plane when the video output
-- reconfigures (a change in size or pixel format). A playlist of same-size still
-- images therefore freezes on the first frame — the playlist clock advances but no
-- KMS page-flip is issued for the next still, so the previous frame keeps scanning
-- out. On each playlist advance, nudge a render-affecting but visually negligible
-- property (video-zoom ~1.0007x, then back to 0) to wake the VO and force a flip.
-- Loaded only for image content, so video playback is unaffected.
local function kick()
    local z = mp.get_property_number("video-zoom", 0) or 0
    mp.set_property_number("video-zoom", (z ~= 0) and 0 or 0.001)
end

mp.observe_property("playlist-pos", "number", function(_, pos)
    if pos == nil then return end
    mp.add_timeout(0.10, kick)   -- two kicks: toggle to 0.001 then back to 0,
    mp.add_timeout(0.25, kick)   -- ending clean with the new image shown at zoom 0
end)
