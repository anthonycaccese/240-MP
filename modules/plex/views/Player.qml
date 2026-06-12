import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()

    property string streamUrl:    navParams.streamUrl    || ""
    property string plexToken:    navParams.plexToken    || ""
    property string ratingKey:    navParams.ratingKey    || ""
    property string partKey:      navParams.partKey      || ""
    property string sessionId:    navParams.sessionId    || ""
    property int    viewOffset:   navParams.viewOffset   || 0
    property string itemTitle:    navParams.title        || ""
    property var    audioStreams:     navParams.audioStreams     || []
    property var    subtitleStreams:  navParams.subtitleStreams  || []
    property int    audioIdx:    0
    property int    subtitleIdx: 0
    property bool   isTranscoding:    navParams.isTranscoding    || false
    property var    imageSubtitleIds: navParams.imageSubtitleIds || []
    property string activeSessionId: sessionId
    property string selectedAudioId:    navParams.selectedAudioId    || ""
    property string selectedSubtitleId: navParams.selectedSubtitleId || "0"

    property bool stoppedReported:    false
    property bool overlayVisible:     false
    property int  choiceIndex:        0
    property string resumeSetting:    "ask"
    property bool pendingZeroStart:   false
    property bool pendingRetryTranscode: false

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

    function stopPlayback() {
        if (!stoppedReported) {
            stoppedReported = true
            var pos = lastKnownPositionMs || absPos(mpvController.position)
            var dur = lastKnownDurationMs || mpvController.duration
            plexBackend.update_timeline(ratingKey, partKey, "stopped", pos, dur)
        }
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
            if (!playerRoot.stoppedReported) {
                var pos = playerRoot.lastKnownPositionMs || playerRoot.absPos(finalPositionMs)
                var dur = playerRoot.lastKnownDurationMs || finalDurationMs
                plexBackend.update_timeline(ratingKey, partKey, "stopped", pos, dur)
            }
            goBack()
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
