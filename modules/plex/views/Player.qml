import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()
    // Emitted when autoplay advances in place, so Root can repoint the BACK
    // target to the now-playing episode's detail instead of the original one.
    signal updateBackItem(var item)

    property string streamUrl:    navParams.streamUrl    || ""
    property string plexToken:    navParams.plexToken    || ""
    property string ratingKey:    navParams.ratingKey    || ""
    property string partKey:      navParams.partKey      || ""
    property string partId:       navParams.partId       || ""
    property string sessionId:    navParams.sessionId    || ""
    property int    viewOffset:   navParams.viewOffset   || 0
    property string itemTitle:    navParams.title        || ""
    property var    audioStreams:     navParams.audioStreams     || []
    property var    subtitleStreams:  navParams.subtitleStreams  || []
    property int    audioIdx:    0
    property int    subtitleIdx: 0
    property bool   isTranscoding:    navParams.isTranscoding    || false
    property var    imageSubtitleIds: navParams.imageSubtitleIds || []
    property string selectedAudioId:    navParams.selectedAudioId    || ""
    property string selectedSubtitleId: navParams.selectedSubtitleId || "0"

    property bool stoppedReported:    false
    property bool overlayVisible:     false
    property int  choiceIndex:        0
    property string resumeSetting:    "ask"
    property bool pendingZeroStart:   false
    property bool pendingRetryTranscode: false

    // Autoplay-next-episode. When enabled, a natural end-of-file advances to the
    // next episode in the same season, carrying over the audio/subtitle language.
    property bool   autoplayNext:       false
    property bool   pendingNextEpisode: false
    property string carryAudioLang:     ""        // language code of the chosen audio track
    property string carrySubLang:       "__off__" // language code, or "__off__" when subtitles are off

    // For transcoded streams, Plex HLS segments have timestamps starting at 0
    // relative to the transcode offset. We add this to every position we report
    // so Plex receives the true absolute position in the video.
    property int transcodeStartOffset: 0

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
                    doStartPlayback(viewOffset)
                } else {
                    startFromBeginning()
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
            } else if (event.key === Qt.Key_Space) {
                mpvController.sendKey("SPACE")
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                mpvController.sendKey("ENTER")
                event.accepted = true
            }
        }
    }

    function newSessionId() {
        var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        var id = ""
        for (var i = 0; i < 12; i++) id += chars[Math.floor(Math.random() * chars.length)]
        return id
    }

    function absPos(streamPosMs) {
        return transcodeStartOffset + streamPosMs
    }

    // Report the final "stopped" timeline for the current episode exactly once.
    // The fallback position/duration are used only when no live value is known.
    function reportStopped(finalPositionMs, finalDurationMs) {
        if (stoppedReported) return
        stoppedReported = true
        var pos = lastKnownPositionMs || absPos(finalPositionMs)
        var dur = lastKnownDurationMs || finalDurationMs
        plexBackend.update_timeline(ratingKey, partKey, "stopped", pos, dur)
    }

    function stopPlayback() {
        reportStopped(mpvController.position, mpvController.duration)
        mpvController.stop()
    }

    function initStreamIndices() {
        var selAudio = navParams.selectedAudioId    || ""
        var selSub   = navParams.selectedSubtitleId || "0"
        for (var i = 0; i < audioStreams.length; i++) {
            if (audioStreams[i].id === selAudio) { audioIdx = i; break }
        }
        for (var j = 0; j < subtitleStreams.length; j++) {
            if (subtitleStreams[j].id === selSub) { subtitleIdx = j; break }
        }
        captureCarryLanguages()
    }

    // Record the language of the current audio/subtitle selection so the next
    // episode (which has different per-file stream IDs) can be matched by language.
    function captureCarryLanguages() {
        var a = audioStreams[audioIdx]
        carryAudioLang = (a && a.language) ? a.language : ""
        // subtitleIdx 0 is the synthetic "OFF" entry — preserve "off" deliberately.
        var s = subtitleStreams[subtitleIdx]
        carrySubLang = (subtitleIdx === 0 || !s) ? "__off__" : (s.language || "")
    }

    // Select audioIdx/subtitleIdx on the current stream lists to match the carried
    // languages. Falls back to the first audio track / subtitles-off when no match.
    function applyCarryLanguages() {
        audioIdx = 0
        for (var i = 0; i < audioStreams.length; i++) {
            if (carryAudioLang && audioStreams[i].language === carryAudioLang) { audioIdx = i; break }
        }
        subtitleIdx = 0
        if (carrySubLang !== "__off__" && carrySubLang !== "") {
            for (var j = 1; j < subtitleStreams.length; j++) {
                if (subtitleStreams[j].language === carrySubLang) { subtitleIdx = j; break }
            }
        }
    }

    function buildSubArgs() {
        var allSubUrls = []
        for (var i = 1; i < subtitleStreams.length; i++) {
            if (subtitleStreams[i] && subtitleStreams[i].subUrl)
                allSubUrls.push(subtitleStreams[i].subUrl)
        }
        var selectedSub = subtitleIdx > 0 ? subtitleStreams[subtitleIdx] : null
        var selectedSubUrl = selectedSub ? (selectedSub.subUrl || "") : ""
        if (selectedSubUrl && allSubUrls.length > 1) {
            allSubUrls = allSubUrls.filter(function(u) { return u !== selectedSubUrl })
            allSubUrls.unshift(selectedSubUrl)
        }
        var subTrack
        if (subtitleIdx === 0)
            subTrack = -1
        else if (selectedSubUrl)
            subTrack = 0
        else
            subTrack = subtitleIdx
        return { urls: allSubUrls, track: subTrack }
    }

    function doStartPlayback(offsetMs) {
        if (isTranscoding) {
            // Transcode URL already encodes the offset from Item.qml; mpv starts at stream position 0.
            // Pass transcodeStartOffset so the OSC Lua script can display accurate wall-clock time.
            mpvController.loadAndPlay(streamUrl, 0.0, 0, -1, [], false, -1, transcodeStartOffset / 1000.0, plexToken)
        } else {
            var sub = buildSubArgs()
            mpvController.loadAndPlay(streamUrl, offsetMs / 1000.0,
                                       audioIdx + 1, sub.track, sub.urls, false, -1, 0.0, plexToken)
        }
    }

    function startFromBeginning() {
        if (isTranscoding) {
            // Transcode URL is baked with viewOffset — request a new session at offset 0.
            // Reset the offset so future position reports are relative to the new start.
            transcodeStartOffset = 0
            pendingZeroStart = true
            plexBackend.request_transcode(ratingKey, partKey, sessionId,
                                          selectedAudioId, selectedSubtitleId, 0)
        } else {
            doStartPlayback(0)
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

    Connections {
        target: plexBackend
        function onErrorOccurred(msg) { console.log("[Player] Backend error: " + msg) }
        function onStreamUrlReady(url, plexToken) {
            if (pendingNextEpisode) {
                // Stream URL for the auto-advanced next episode just arrived.
                pendingNextEpisode = false
                playerRoot.streamUrl = url
                playerRoot.plexToken = plexToken
                doStartPlayback(0)
                return
            }
            if (pendingRetryTranscode) {
                pendingRetryTranscode = false
                isTranscoding = true
                transcodeStartOffset = viewOffset
                var sub = buildSubArgs()
                mpvController.loadAndPlay(url, 0.0, audioIdx + 1, sub.track, sub.urls, false, -1, transcodeStartOffset / 1000.0, plexToken)
                return
            }
            if (!pendingZeroStart) return
            pendingZeroStart = false
            var sub = buildSubArgs()
            mpvController.loadAndPlay(url, 0.0, audioIdx + 1, sub.track, sub.urls, false, -1, 0.0, plexToken)
        }

        function onNextEpisodeReady(detail) {
            if (!pendingNextEpisode) return
            // Empty detail → no next episode in the season (or a lookup failure).
            // Fall back to the standard behavior: return to the detail view.
            if (!detail || !detail.ratingKey) {
                pendingNextEpisode = false
                goBack()
                return
            }
            playerRoot.advanceToEpisode(detail)
        }
    }

    // Swap the player's context to the next episode in place (no navigation) and
    // begin playing it from the beginning, carrying over the track languages.
    function advanceToEpisode(detail) {
        ratingKey   = detail.ratingKey
        partKey     = detail.partKey      || ""
        partId      = detail.partId       || ""
        itemTitle   = detail.title        || ""
        audioStreams    = detail.audioStreams    || []
        subtitleStreams = detail.subtitleStreams || []
        isTranscoding   = detail.forceTranscode  || false

        // Recompute the image-subtitle IDs for THIS episode (stream IDs are
        // per-file), mirroring Item.qml's build before the initial hand-off.
        var imageSubs = []
        for (var k = 0; k < subtitleStreams.length; k++) {
            if (subtitleStreams[k] && subtitleStreams[k].imageSubtitle)
                imageSubs.push(subtitleStreams[k].id)
        }
        imageSubtitleIds = imageSubs

        // Fresh-start state for the new episode.
        viewOffset           = 0
        transcodeStartOffset = 0
        stoppedReported      = false
        lastKnownPositionMs  = 0
        lastKnownDurationMs  = 0
        sessionId            = newSessionId()

        // Repoint the BACK target so exiting returns to THIS episode's detail
        // screen, not the one we auto-advanced from. Item.qml reloads from
        // item.ratingKey, so a minimal item carrying the new keys suffices.
        updateBackItem({
            ratingKey: detail.ratingKey,
            type: detail.type || "episode",
            title: detail.title || "",
            grandparentTitle: detail.grandparentTitle || "",
            parentIndex: detail.parentIndex,
            index: detail.index
        })

        // Match the carried languages onto this episode's stream lists, then
        // remember the resulting selection for the episode after this one.
        applyCarryLanguages()
        var audioId = (audioStreams[audioIdx] && audioStreams[audioIdx].id) ? audioStreams[audioIdx].id : ""
        var subId   = (subtitleStreams[subtitleIdx] && subtitleStreams[subtitleIdx].id) ? subtitleStreams[subtitleIdx].id : "0"
        selectedAudioId    = audioId
        selectedSubtitleId = subId
        captureCarryLanguages()

        // Persist the chosen tracks to Plex so a transcode burns the right streams
        // (mirrors Item.qml's behavior before playback).
        if (partId) {
            if (audioId) plexBackend.set_audio_stream(audioId, partId)
            plexBackend.set_subtitle_stream(subId, partId)
        }

        // Both paths resolve through onStreamUrlReady, which checks this flag.
        // build_stream_url emits synchronously, so the flag must be set first.
        pendingNextEpisode = true
        if (isTranscoding) {
            plexBackend.request_transcode(ratingKey, partKey, sessionId, audioId, subId, 0)
        } else {
            plexBackend.build_stream_url(ratingKey, partKey, sessionId)
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) playerRoot.lastKnownPositionMs = playerRoot.absPos(ms)
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }

        function onPlaybackFinished(finalPositionMs, finalDurationMs) {
            // mpv exited because the user quit/stopped — return to the detail view.
            reportStopped(finalPositionMs, finalDurationMs)
            goBack()
        }

        function onPlaybackFinishedNaturally(finalPositionMs, finalDurationMs) {
            // mpv reached the end of the file. Mark it stopped (watched) in Plex,
            // then auto-advance to the next episode if the feature is enabled.
            reportStopped(finalPositionMs, finalDurationMs)
            if (!autoplayNext) { goBack(); return }
            pendingNextEpisode = true
            plexBackend.load_next_episode(ratingKey)
        }

        function onPlaybackFailed() {
            if (!isTranscoding) {
                // Direct play failed (e.g. HTTP 500 from PMS on WAN). Retry
                // transparently with transcoding at the same resume offset.
                pendingRetryTranscode = true
                plexBackend.request_transcode(ratingKey, partKey, sessionId,
                                              selectedAudioId, selectedSubtitleId,
                                              viewOffset)
            } else {
                goBack()
            }
        }
    }

    Timer {
        interval: 10000
        repeat:   true
        running:  true
        onTriggered: {
            if (mpvController.position > 0)
                plexBackend.update_timeline(ratingKey, partKey, "playing",
                                            absPos(mpvController.position), mpvController.duration)
        }
    }

    Component.onCompleted: {
        initStreamIndices()
        if (streamUrl === "") return
        resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        // Match ModuleSettings.qml's reading of a toggle: stored as a real bool
        // once the user touches it, but accept the legacy "ON" string too.
        var autoplayRaw = appCore.get_setting(moduleRoot.moduleId, "autoplay_next_episode")
        autoplayNext  = (autoplayRaw === true || autoplayRaw === "ON")

        // Plex HLS transcode segments start at time 0 regardless of viewOffset.
        // Track the offset so every reported position is absolute in the video.
        transcodeStartOffset = isTranscoding ? viewOffset : 0

        if (resumeSetting === "ask" && viewOffset > 0) {
            overlayVisible = true
        } else {
            doStartPlayback(viewOffset)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
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
