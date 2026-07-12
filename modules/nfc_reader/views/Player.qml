import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string videoPath:  navParams.videoPath || ""
    property string videoTitle: navParams.title || ""

    property bool   playbackStarted: false
    property string errorMessage:    ""
    property int    lastStartMs:     0   // what the last attempt started from, for retry

    // Track last non-null values during playback; groundwork for a future
    // resume-playback setting (mirrors the other module players).
    property int    lastKnownPositionMs: 0
    property int    lastKnownDurationMs: 0

    focus: true

    function doPlay(startMs) {
        lastStartMs = startMs
        // extraArgs opts into yt-dlp so YouTube-page URLs in the mapping
        // resolve; safe for local files and direct media URLs, which the
        // native demuxer handles before the ytdl hook ever runs.
        mpvController.loadAndPlay(videoPath, startMs / 1000.0, -1, -1, [], [], false, -1, 0.0, "", false, "", false, [], 0.0, false, ["--ytdl=yes"])
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
            // A bad mapping path or missing yt-dlp surfaces as an mpv failure
            // before any position event — show the error instead of leaving.
            if (reason === "failed" && !playbackStarted) {
                playerRoot.errorMessage = "PLAYBACK FAILED\n\nCHECK THE MAPPED PATH OR URL\n(YOUTUBE LINKS REQUIRE YT-DLP)"
                return
            }
            goBack()
        }
    }

    Component.onCompleted: {
        if (videoPath === "") {
            goBack()
            return
        }
        play(0)
    }

    // Every exit path (natural end, user quit, backing out of the error
    // screen) must re-arm the backend or it keeps ignoring card taps.
    Component.onDestruction: nfcReaderBackend.resetAfterPlayback()

    Rectangle {
        anchors.fill: parent
        color: "black"

        // Shown while mpv launches and (for YouTube URLs) yt-dlp resolves the
        // stream. Hidden once the first position update arrives.
        Text {
            text: "LOADING..."
            color: "white"
            font.family: root.globalFont
            anchors.centerIn: parent
            font.pixelSize: root.sh * 0.05 //24
            visible: !playbackStarted && errorMessage === ""
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
}
