import QtQuick

// Live playback: hand the stream URL straight to mpv. No resume, seek, or
// progress reporting — this is a live feed, not VOD.
FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string streamUrl: navParams.streamUrl || ""
    property string channelTitle: navParams.title || ""
    property bool playbackStarted: false

    focus: true

    Keys.onPressed: function(event) {
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

    // Defer the launch one tick so the LOADING frame paints before mpv takes
    // over the screen (mirrors the other modules' players).
    Timer {
        id: startTimer
        interval: 16
        repeat: false
        onTriggered: {
            // audioTrack -1 (mpv default), subTrack -2 (subs off) — live streams
            // rarely carry selectable tracks; everything else uses defaults.
            mpvController.loadAndPlay(playerRoot.streamUrl, 0.0, -1, -2)
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) playerRoot.playbackStarted = true
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            playerRoot.goBack()
        }
    }

    Component.onCompleted: {
        if (streamUrl !== "") startTimer.restart()
    }

    Rectangle {
        anchors.fill: parent
        color: "black"

        Text {
            text: "TUNING IN..."
            color: "white"
            font.family: root.globalFont
            anchors.centerIn: parent
            font.pixelSize: root.sh * 0.05
            visible: playerRoot.streamUrl !== "" && !playerRoot.playbackStarted
        }
    }
}
