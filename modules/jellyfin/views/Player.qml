import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()
    // Emitted when autoplay advances in place, so Root can repoint the BACK
    // target to the now-playing episode's detail instead of the original one.
    signal updateBackItem(var item)

    property string streamUrl:      navParams.streamUrl      || ""
    property string itemId:         navParams.itemId         || ""
    property string seriesId:       navParams.seriesId       || ""
    property string mediaSourceId:  navParams.mediaSourceId  || itemId
    property string itemTitle:      navParams.title          || ""
    property int    viewOffset:     navParams.viewOffset     || 0
    property int    parentIndex:    navParams.parentIndex    || 0
    property int    index:          navParams.index          || 0
    property var    audioStreams:       navParams.audioStreams     || []
    property var    subtitleStreams:    navParams.subtitleStreams  || []
    property string selectedAudioId:    navParams.selectedAudioId    || ""
    property string selectedSubtitleId: navParams.selectedSubtitleId || ""

    property bool isTranscoding: streamUrl.indexOf("master.m3u8") >= 0

    // Autoplay next episode
    property bool   autoplayNext:       false
    property bool   pendingNextEpisode: false
    // Set when a direct-play attempt fails and we re-request forcing a transcode.
    property bool   pendingRetryTranscode: false
    property string carryAudioLang:     ""
    property string carrySubLang:       "__off__"

    property int    audioIdx:    0
    property int    subtitleIdx: -1

    property bool stoppedReported: false
    property bool playbackStarted: false
    property bool overlayVisible:  false
    property int  choiceIndex:     0
    property string resumeSetting: "ask"

    // Intro/outro skip
    property var    segments:           []
    property var    activeSegment:      null
    property bool   skipPromptShown:    false
    property bool   introAutoSkipped:   false
    property bool   outroAutoSkipped:   false
    property string introSkipSetting:   "Off"
    property string outroSkipSetting:   "Off"

    property int lastKnownPositionMs: 0
    property int lastKnownDurationMs: 0

    focus: true

    Keys.onPressed: function(event) {
        if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                choiceIndex = 0
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                choiceIndex = 1
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                overlayVisible = false
                if (choiceIndex === 0) {
                    beginPlayback(viewOffset)
                } else {
                    beginPlayback(0)
                }
                event.accepted = true
            }
        } else {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                mpvController.sendKey("ESC")
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                mpvController.sendKey("BS")
                event.accepted = true
            } else if (event.key === Qt.Key_Space) {
                mpvController.sendKey("SPACE")
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                mpvController.sendKey("UP")
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                mpvController.sendKey("DOWN")
                event.accepted = true
            } else if (event.key === Qt.Key_Left) {
                mpvController.sendKey("LEFT")
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                mpvController.sendKey("RIGHT")
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                mpvController.sendKey("ENTER")
                event.accepted = true
            }
        }
    }

    function msToTicks(ms) {
        return ms * 10000
    }

    function initStreamIndices() {
        var selAudio = String(selectedAudioId || "")
        var selSub   = String(selectedSubtitleId || "")
        audioIdx = 0
        for (var i = 0; i < audioStreams.length; i++) {
            if (String(audioStreams[i].id || "") === selAudio) { audioIdx = i; break }
        }
        // Subtitle track: -1 means off; otherwise find the 0-based index in subtitleStreams.
        subtitleIdx = -1
        for (var j = 0; j < subtitleStreams.length; j++) {
            if (String(subtitleStreams[j].id || "") === selSub) { subtitleIdx = j; break }
        }
        captureCarryLanguages()
    }

    // Record the language of the current audio/subtitle selection so the next
    // episode (which has different per-file stream IDs) can be matched by language.
    function captureCarryLanguages() {
        var a = audioStreams[audioIdx]
        carryAudioLang = (a && a.language) ? a.language : ""
        var s = subtitleStreams[subtitleIdx]
        carrySubLang = (subtitleIdx === -1 || !s) ? "__off__" : (s.language || "")
    }

    // Select audioIdx/subtitleIdx on the current stream lists to match the carried
    // languages. Falls back to the first audio track / subtitles-off when no match.
    function applyCarryLanguages() {
        audioIdx = 0
        for (var i = 0; i < audioStreams.length; i++) {
            if (carryAudioLang && audioStreams[i].language === carryAudioLang) { audioIdx = i; break }
        }
        subtitleIdx = -1
        if (carrySubLang !== "__off__" && carrySubLang !== "") {
            for (var j = 0; j < subtitleStreams.length; j++) {
                if (subtitleStreams[j].language === carrySubLang) { subtitleIdx = j; break }
            }
        }
    }

    function reportStopped(finalPositionMs, finalDurationMs, failed) {
        if (stoppedReported) return
        stoppedReported = true
        var pos = lastKnownPositionMs || finalPositionMs
        jellyfinBackend.report_playback_stopped(itemId, mediaSourceId, msToTicks(pos), failed || false)
    }

    function stopPlayback() {
        reportStopped(mpvController.position, mpvController.duration, lastKnownPositionMs <= 0)
        mpvController.stop()
    }

    // Swap the player's context to the next episode in place (no navigation) and
    // begin playing it from the beginning, carrying over the track languages.
    function advanceToEpisode(detail) {
        itemId         = detail.itemId         || ""
        mediaSourceId  = detail.mediaSourceId  || detail.itemId || ""
        itemTitle      = detail.title          || ""
        audioStreams   = detail.audioStreams   || []
        subtitleStreams= detail.subtitleStreams|| []
        seriesId       = detail.seriesId       || ""
        parentIndex    = detail.parentIndex    || 0
        index          = detail.index          || 0

        // Fresh-start state for the new episode
        viewOffset           = 0
        stoppedReported      = false
        playbackStarted      = false
        lastKnownPositionMs  = 0
        lastKnownDurationMs  = 0

        // Reset skip state for the new episode
        segments = []
        activeSegment = null
        skipPromptShown = false
        introAutoSkipped = false
        outroAutoSkipped = false
        mpvController.clearOsdPrompt()

        // Repoint the BACK target so exiting returns to THIS episode's detail
        updateBackItem({
            itemId: detail.itemId,
            type: detail.type || "episode",
            title: detail.title || "",
            grandparentTitle: detail.grandparentTitle || "",
            parentIndex: detail.parentIndex,
            index: detail.index
        })

        // Match the carried languages onto this episode's stream lists
        applyCarryLanguages()
        selectedAudioId    = (audioStreams[audioIdx] && audioStreams[audioIdx].id) ? String(audioStreams[audioIdx].id) : ""
        selectedSubtitleId = (subtitleIdx >= 0 && subtitleStreams[subtitleIdx] && subtitleStreams[subtitleIdx].id) ? String(subtitleStreams[subtitleIdx].id) : ""
        captureCarryLanguages()

        // Request the new stream URL — get_playback_url() reports the playback
        // Start to the server once PlaybackInfo resolves (correct session/method).
        pendingNextEpisode = true
        var audioStreamIdx = selectedAudioId ? parseInt(selectedAudioId) : -1
        var subStreamIdx   = selectedSubtitleId ? parseInt(selectedSubtitleId) : -1
        jellyfinBackend.get_playback_url(detail.itemId, detail.mediaSourceId || detail.itemId,
                                          audioStreamIdx, subStreamIdx)
    }

    // Starting mpv runs synchronously and, on the Pi, immediately switches VT
    // (suspending Qt's render thread) before the LOADING frame can paint. Defer
    // the launch one tick so the loading indicator is rendered first.
    Timer {
        id: startTimer
        interval: 50
        repeat: false
        property int pendingOffset: 0
        onTriggered: doStartPlayback(pendingOffset)
    }

    function beginPlayback(offsetMs) {
        startTimer.pendingOffset = offsetMs
        startTimer.restart()
    }

    // Mirrors PlexBackend's Player.buildSubArgs: text subtitles are handed to mpv
    // as sidecar --sub-file URLs (so direct play never transcodes to show them),
    // while image subs (no subUrl) are selected from the embedded stream via --sid.
    // subtitleIdx is -1 for off, otherwise a 0-based index into subtitleStreams.
    // Friendly track name for a sidecar — mpv would otherwise title it from the
    // opaque sidecar URL. Passed to the OSC alongside the URL (see loadAndPlay).
    function subLabel(s) {
        return (s.displayTitle || s.title || s.language || s.codec || "")
    }

    function buildSubArgs() {
        var pairs = []
        for (var i = 0; i < subtitleStreams.length; i++) {
            var s = subtitleStreams[i]
            if (s && s.subUrl)
                pairs.push({ url: s.subUrl, title: subLabel(s) })
        }
        var selectedSub = subtitleIdx >= 0 ? subtitleStreams[subtitleIdx] : null
        var selectedSubUrl = selectedSub ? (selectedSub.subUrl || "") : ""
        // Put the selected sidecar first so mpv auto-selects it (subTrack 0).
        if (selectedSubUrl && pairs.length > 1) {
            pairs = pairs.filter(function(p) { return p.url !== selectedSubUrl })
            pairs.unshift({ url: selectedSubUrl, title: subLabel(selectedSub) })
        }
        var subTrack
        if (subtitleIdx < 0)
            subTrack = -1                 // off → forced subs only (matches Plex)
        else if (selectedSubUrl)
            subTrack = 0                  // selected sidecar is the first loaded sub-file
        else
            subTrack = subtitleIdx + 1    // embedded/image sub → mpv 1-based --sid
        return {
            urls:   pairs.map(function(p) { return p.url }),
            titles: pairs.map(function(p) { return p.title }),
            track:  subTrack
        }
    }

    function doStartPlayback(offsetMs) {
        if (isTranscoding) {
            // HLS manifest bakes in the selected audio, and the chosen subtitle is
            // burned into the video — so there's no soft sub track for mpv to pick
            // (subTrack -1 = forced-only, a no-op when nothing soft exists).
            mpvController.loadAndPlay(streamUrl, offsetMs / 1000.0,
                                       -1, -1, [], [], false, -1, 0.0, "")
        } else {
            // Direct play: file served whole. audioIdx is 0-based → mpv's 1-based
            // --aid; subtitles come from buildSubArgs (sidecars + --sid).
            var audioTrack = audioStreams.length > 0 ? audioIdx + 1 : 0
            var sub = buildSubArgs()
            mpvController.loadAndPlay(streamUrl, offsetMs / 1000.0,
                                       audioTrack, sub.track, sub.urls, [], false, -1, 0.0, "",
                                       false, "", false, sub.titles)
        }
    }

    function formatTime(ms) {
        var s = Math.floor(ms / 1000)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        if (h > 0)
            return h + ":" + (m < 10 ? "0" : "") + m + ":" + (sec < 10 ? "0" : "") + sec
        return m + ":" + (sec < 10 ? "0" : "") + sec
    }

    function findActiveSegment(ms) {
        for (var i = 0; i < segments.length; i++) {
            if (ms >= segments[i].startMs && ms < segments[i].endMs)
                return segments[i]
        }
        return null
    }

    Connections {
        target: jellyfinBackend
        function onErrorOccurred(msg) { console.log("[Jellyfin Player] Backend error: " + msg) }

        function onSegmentsReady(itemId_, segments_) {
            if (itemId_ !== playerRoot.itemId) return
            playerRoot.segments = segments_
        }

        function onStreamUrlReady(url) {
            if (pendingNextEpisode) {
                // Stream URL for the auto-advanced next episode just arrived
                pendingNextEpisode = false
                playerRoot.streamUrl = url
                doStartPlayback(0)
                // Fetch segments for intro/outro skip after playback starts, so
                // the HTTP request doesn't contend with the PlaybackInfo POST.
                introSkipSetting = appCore.get_setting(moduleRoot.moduleId, "intro_skip") || "Off"
                outroSkipSetting = appCore.get_setting(moduleRoot.moduleId, "outro_skip") || "Off"
                if (introSkipSetting !== "Off" || outroSkipSetting !== "Off")
                    jellyfinBackend.fetchSegments(playerRoot.itemId)
                return
            }
            if (pendingRetryTranscode) {
                // Fallback transcode after a direct-play failure. The transcode
                // covers the full timeline from 0, so seek mpv to where we left off.
                pendingRetryTranscode = false
                playerRoot.streamUrl = url
                playerRoot.isTranscoding = true
                doStartPlayback(lastKnownPositionMs > 0 ? lastKnownPositionMs : viewOffset)
                return
            }
        }

        function onNextEpisodeReady(detail) {
            if (!pendingNextEpisode) return
            // Empty detail → no next episode in the season
            if (!detail || !detail.itemId) {
                pendingNextEpisode = false
                goBack()
                return
            }
            playerRoot.advanceToEpisode(detail)
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) {
                playerRoot.lastKnownPositionMs = ms
                // First position update means mpv is up and playing — drop the
                // loading indicator (mpv's own window now covers the screen).
                playerRoot.playbackStarted = true

                // --- Skip segment tracking ---
                if (playerRoot.segments.length > 0) {
                    var seg = findActiveSegment(ms)
                    if (seg && seg !== playerRoot.activeSegment) {
                        playerRoot.activeSegment = seg
                        var setting = seg.type === "Intro"
                            ? playerRoot.introSkipSetting
                            : playerRoot.outroSkipSetting

                        if (setting === "Auto") {
                            if (seg.type === "Intro" && !playerRoot.introAutoSkipped) {
                                playerRoot.introAutoSkipped = true
                                mpvController.seekTo(seg.endMs)
                            } else if (seg.type === "Outro" && !playerRoot.outroAutoSkipped) {
                                playerRoot.outroAutoSkipped = true
                                mpvController.seekTo(seg.endMs)
                            }
                        } else if (setting === "Button") {
                            if (!playerRoot.skipPromptShown) {
                                playerRoot.skipPromptShown = true
                                mpvController.showOsdSkipPrompt()
                            }
                        }
                    } else if (!seg && playerRoot.activeSegment) {
                        // Segment ended naturally
                        playerRoot.activeSegment = null
                        playerRoot.skipPromptShown = false
                        mpvController.clearOsdPrompt()
                    }
                }
                // --- End skip segment tracking ---
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }

        function onSkipRequested() {
            if (playerRoot.activeSegment) {
                if (playerRoot.activeSegment.type === "Intro")
                    playerRoot.introAutoSkipped = true
                else
                    playerRoot.outroAutoSkipped = true
                mpvController.seekTo(playerRoot.activeSegment.endMs)
                mpvController.clearOsdPrompt()
                // Don't null activeSegment here — the async seek moves past the
                // segment boundary, and the next onPositionChanged detects the
                // end naturally. Nulling it before the seek completes causes a
                // position update at the old location to re-detect the same
                // segment as "new" and re-trigger the prompt.
            }
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            if (reason === "failed") {
                if (!isTranscoding) {
                    // Direct play failed (e.g. a codec mpv couldn't handle, or a
                    // network drop). Retry transparently with a transcode, resuming
                    // at the last known position. Mirrors the Plex module.
                    pendingRetryTranscode = true
                    var aIdx = selectedAudioId ? parseInt(selectedAudioId) : -1
                    var sIdx = selectedSubtitleId ? parseInt(selectedSubtitleId) : -1
                    jellyfinBackend.get_playback_url(itemId, mediaSourceId, aIdx, sIdx, true)
                    return
                }
                // mpv exited with an error. Report as failed so the server
                // doesn't update the resume position. reportStopped uses the
                // last known position internally, so this is safe even when
                // mpv exited before the first position update.
                reportStopped(finalPositionMs, finalDurationMs, true)
                goBack()
                return
            }

            // Both a natural end ("eof") and user quit ("stopped") save
            // the current position for resume. A natural end only attempts
            // to auto-advance when the user has autoplay enabled.
            reportStopped(finalPositionMs, finalDurationMs)
            if (reason === "eof" && autoplayNext) {
                pendingNextEpisode = true
                jellyfinBackend.load_next_episode(itemId)
                return
            }
            goBack()
        }

    }

    Timer {
        interval: 10000
        repeat:   true
        running:  true
        onTriggered: {
            if (mpvController.position > 0)
                jellyfinBackend.update_playback_progress(itemId, mediaSourceId,
                                                         msToTicks(mpvController.position), false)
        }
    }

    Component.onCompleted: {
        initStreamIndices()
        if (streamUrl === "") return
        resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        // Match ModuleSettings.qml's reading of a toggle: stored as a real bool
        // once the user touches it, but accept the legacy "ON" string too.
        var autoplayRaw = appCore.get_setting(moduleRoot.moduleId, "autoplay_next_episode")
        autoplayNext = (autoplayRaw === true || autoplayRaw === "ON")

        // Hoist skip settings
        introSkipSetting = appCore.get_setting(moduleRoot.moduleId, "intro_skip") || "Off"
        outroSkipSetting = appCore.get_setting(moduleRoot.moduleId, "outro_skip") || "Off"

        // Fetch segments if either skip mode is enabled
        if (introSkipSetting !== "Off" || outroSkipSetting !== "Off")
            jellyfinBackend.fetchSegments(itemId)

        // "ask": prompt resume vs. start over when there's a saved position.
        // "always" (or anything else): resume directly.
        if (resumeSetting === "ask" && viewOffset > 0) {
            overlayVisible = true
        } else {
            beginPlayback(viewOffset)
        }
    }

    // Safety net: if the Player view is destroyed (e.g. app quit, back nav
    // without stopping), report stopped so Jellyfin doesn't show this as
    // still playing. The guard in reportStopped prevents double-reporting.
    Component.onDestruction: {
        if (lastKnownPositionMs > 0)
            reportStopped(lastKnownPositionMs, lastKnownDurationMs)
    }

    Rectangle {
        anchors.fill: parent
        color: "black"

        // Shown while mpv launches and buffers the stream (before its window
        // takes over). Hidden once the first position update arrives, or while
        // the resume prompt is up.
        Text {
            text: "LOADING..."
            // White to match mpv's own overlay text color.
            color: "white"
            font.family: root.globalFont
            anchors.centerIn: parent
            font.pixelSize: root.sh * 0.05 //24
            visible: streamUrl !== "" && !overlayVisible && !playbackStarted
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: overlayVisible

        Rectangle {
            id: dialogRect
            color: root.surfaceColor
            anchors.centerIn: parent
            width: root.sw * 0.76875
            height: root.sh * 0.2833333

            Column {
                id: dialogColumn
                anchors.fill: parent
                spacing: root.sh * 0.05

                Text {
                    text: "RESUME PLAYBACK?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: [
                            "Resume from " + formatTime(viewOffset),
                            "Start from the beginning"
                        ]
                        delegate: Item {
                            width: dialogColumn.width
                            height: root.sh * 0.0583333

                            Rectangle {
                                anchors.fill: delegateText
                                color: root.accentColor
                                visible: index === choiceIndex
                            }

                            Text {
                                id: delegateText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData
                                color: index === choiceIndex ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.capitalization: Font.AllUppercase
                                topPadding: root.sh * 0.0041667
                                leftPadding: root.sw * 0.009375
                                rightPadding: root.sw * 0.009375
                                bottomPadding: root.sh * 0.00625
                                font.pixelSize: root.sh * 0.0416667
                            }
                        }
                    }
                }

                Text {
                    text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }
}
