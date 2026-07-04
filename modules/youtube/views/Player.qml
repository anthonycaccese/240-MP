import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property var    item:     navParams.item || ({})
    property string videoUrl: item.url || ""
    property string videoId:  item.videoId || ""

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
        mpvController.loadAndPlay(videoUrl, startMs / 1000.0, 0, -2, [], [], false, -1, 0.0, "", false, "", false, [], 0.0, false, ytdlArgs)
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

    Keys.onPressed: function(event) {
        if (errorMessage !== "") {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                errorMessage = ""
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
            goBack()
        }
    }

    Component.onCompleted: {
        if (videoUrl === "") {
            goBack()
            return
        }
        var resolution = appCore.get_setting(moduleRoot.moduleId, "playback_resolution") || "480p"
        ytdlArgs = ["--ytdl=yes", "--ytdl-format=" + youtubeBackend.ytdlFormatForResolution(resolution)]

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
