import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()
    // Emitted when autoplay advances in place, so Root can move the list
    // cursor BACK returns to onto the now-playing playlist item.
    signal updateBackListState(var state)

    property var    item:     navParams.item || ({})
    property string videoUrl: item.url || ""
    property string videoId:  item.videoId || ""
    property var    playlistItems: navParams.playlistItems || []
    property int    playlistIndex: navParams.playlistIndex === undefined ? -1 : navParams.playlistIndex

    // Shuffle mode: entered with a playlistId instead of an item. The playlist's
    // videos are fetched here, shuffled, and handed to mpv as one playlist so it
    // advances on its own. `item` is re-pointed at each entry as mpv moves through
    // the queue, which keeps the history/resume code below working unchanged.
    property string playlistId:   navParams.playlistId || ""
    property string playlistName: navParams.playlistName || ""
    property bool   shuffleMode:  playlistId !== ""
    property var    queue:        []
    property int    queuePos:     0

    property bool   overlayVisible:   false
    property bool   playbackStarted:  false
    property int    savedPositionMs:  0
    property int    choiceIndex:      0
    property string errorMessage:     ""
    property var    ytdlArgs:         []
    property int    lastStartMs:      0   // what the last attempt started from, for retry

    // Track last non-null values during playback for robust save on exit
    property int    lastKnownPositionMs: 0
    property int    lastKnownDurationMs: 0

    focus: true

    function doPlay(startMs) {
        overlayVisible = false
        lastStartMs = startMs
        // Bare URLs in extraArgs land after the first one on mpv's command line,
        // which is how mpv builds a playlist. Deliberately NOT the playlist URL
        // with --shuffle: mpv expands that through yt-dlp up front, which took
        // over 3 minutes on the Pi. Per-video URLs are resolved lazily instead.
        var extra = ytdlArgs
        for (var i = 1; i < queue.length; i++)
            extra = extra.concat([queue[i].url])
        mpvController.loadAndPlay(videoUrl, startMs / 1000.0, 0, -2, [], [], false, -1, 0.0, "", false, "", false, [], 0.0, false, extra)
    }

    function shuffled(list) {
        var a = list.slice()
        for (var i = a.length - 1; i > 0; i--) {
            var j = Math.floor(Math.random() * (i + 1))
            var t = a[i]; a[i] = a[j]; a[j] = t
        }
        return a
    }

    // Same rule as the list views: unset/true/"ON" shows Shorts, explicit false hides.
    function filterShorts(videos) {
        var raw = appCore.get_setting(moduleRoot.moduleId, "display_shorts")
        var showShorts = (raw === undefined || raw === null) ? true
                         : (raw === true || raw === "ON")
        if (showShorts)
            return videos
        return videos.filter(function(v) { return !v.isShort })
    }

    function startShuffle() {
        errorMessage = ""
        youtubeBackend.load_playlist_videos(playlistId)
    }

    // Starting mpv runs synchronously and, on the Pi, immediately switches VT
    // (suspending Qt's render thread) before the LOADING frame can paint. Defer
    // the launch one tick so the loading indicator is rendered first.
    Timer {
        id: startTimer
        interval: 50
        repeat: false
        property int pendingStartMs: 0
        onTriggered: doPlay(pendingStartMs)
    }

    function play(startMs) {
        startTimer.pendingStartMs = startMs
        startTimer.restart()
    }

    function playNextPlaylistItem() {
        if (playlistIndex < 0 || playlistIndex + 1 >= playlistItems.length)
            return false
        item = playlistItems[++playlistIndex]
        playbackStarted = false
        lastKnownPositionMs = 0
        lastKnownDurationMs = 0
        savedPositionMs = 0
        updateBackListState({ currentIndex: playlistIndex })
        play(0)
        return true
    }

    Keys.onPressed: function(event) {
        if (errorMessage !== "") {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                errorMessage = ""
                // Nothing to replay yet if the playlist fetch itself failed.
                if (shuffleMode && queue.length === 0)
                    startShuffle()
                else
                    play(lastStartMs)
                event.accepted = true
            }
        } else if (overlayVisible) {
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
                play(choiceIndex === 0 ? savedPositionMs : 0)
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

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) {
                playerRoot.playbackStarted = true
                playerRoot.lastKnownPositionMs = ms
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }

        // mpv advanced to the next shuffled entry. Its playlist is our queue in
        // the same order, so pos indexes straight into it. Bank the finished
        // video as watched and re-point `item` before the counters reset.
        function onPlaylistPosChanged(pos) {
            if (!playerRoot.shuffleMode || pos === playerRoot.queuePos)
                return
            var finished = playerRoot.queue[playerRoot.queuePos]
            if (finished)
                youtubeBackend.savePosition(finished.videoId, 0,
                                            finished.title || "", finished.channelName || "")
            playerRoot.queuePos = pos
            if (playerRoot.queue[pos])
                playerRoot.item = playerRoot.queue[pos]
            playerRoot.lastKnownPositionMs = 0
            playerRoot.lastKnownDurationMs = 0
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            // yt-dlp missing/outdated or an unplayable stream surfaces as mpv exit
            // code 2 before any position event — show the error instead of leaving.
            if (reason === "failed" && !playbackStarted) {
                playerRoot.errorMessage = "PLAYBACK FAILED\n\nPLEASE CHECK THAT YT-DLP IS INSTALLED AND UP TO DATE"
                return
            }
            var pos = lastKnownPositionMs || finalPositionMs
            var dur = lastKnownDurationMs || finalDurationMs
            // Completed videos stay in history with pos 0: they list under
            // History but never trigger the resume prompt.
            if (dur > 0 && pos >= dur * 0.95)
                youtubeBackend.savePosition(videoId, 0, item.title || "", item.channelName || "")
            else if (pos > 5000)
                youtubeBackend.savePosition(videoId, pos, item.title || "", item.channelName || "")
            var nextItemEnabled = appCore.get_setting(moduleRoot.moduleId, "autoplay_playlist_items")
            if (reason === "eof" && (nextItemEnabled === true || nextItemEnabled === "ON")
                    && playerRoot.playNextPlaylistItem())
                return
            goBack()
        }
    }

    Connections {
        target: youtubeBackend
        enabled: playerRoot.shuffleMode

        function onPlaylistVideosLoaded(loadedPlaylistId, videos) {
            if (loadedPlaylistId !== playerRoot.playlistId)
                return
            var vids = playerRoot.filterShorts(videos)
            if (vids.length === 0) {
                playerRoot.errorMessage = "NO VIDEOS IN THIS PLAYLIST"
                return
            }
            playerRoot.queue    = playerRoot.shuffled(vids)
            playerRoot.queuePos = 0
            playerRoot.item     = playerRoot.queue[0]
            // Shuffle always starts fresh — a saved position belongs to one video,
            // which says nothing about where a randomized run should begin.
            playerRoot.play(0)
        }

        function onErrorOccurred(msg) {
            playerRoot.errorMessage = msg
        }
    }

    Component.onCompleted: {
        var resolution = appCore.get_setting(moduleRoot.moduleId, "playback_resolution") || "480p"
        ytdlArgs = ["--ytdl=yes", "--ytdl-format=" + youtubeBackend.ytdlFormatForResolution(resolution)]

        if (shuffleMode) {
            startShuffle()
            return
        }
        if (videoUrl === "") {
            goBack()
            return
        }

        var resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "Ask"
        var saved = youtubeBackend.getSavedPosition(videoId)
        var savedPos = saved.pos || 0

        if (resumeSetting === "Always") {
            play(savedPos)
        } else if (savedPos > 0) {
            savedPositionMs = savedPos
            overlayVisible = true
        } else {
            play(0)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"

        // Shown while mpv launches and buffers the stream (before its window
        // takes over). Hidden once the first position update arrives, or while
        // the resume prompt is up.
        Text {
            text: "LOADING..."
            color: "white"
            font.family: root.globalFont
            anchors.centerIn: parent
            font.pixelSize: root.sh * 0.05 //24
            visible: !overlayVisible && !playbackStarted && errorMessage === ""
        }

        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.05 //24
            visible: errorMessage !== ""

            Text {
                text: errorMessage
                color: "white"
                font.family: root.globalFont
                width: root.sw * 0.5625 //360
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: root.sh * 0.0375 //18
            }
            Text {
                text: root.hints.back + ":BACK " + root.hints.select + ":RETRY"
                color: "#919191"
                font.family: root.globalFont
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: root.sh * 0.0333333 //16
            }
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
                            "Resume from " + formatTime(savedPositionMs),
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

    function formatTime(ms) {
        var s   = Math.floor(ms / 1000)
        var h   = Math.floor(s / 3600)
        var m   = Math.floor((s % 3600) / 60)
        var sec = s % 60
        if (h > 0)
            return h + ":" + pad(m) + ":" + pad(sec)
        return m + ":" + pad(sec)
    }

    function pad(n) { return n < 10 ? "0" + n : "" + n }
}
