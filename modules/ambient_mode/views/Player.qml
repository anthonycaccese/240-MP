import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string videoPath:      navParams.videoPath || ""
    property string audioPath:      navParams.audioPath || ""
    property bool   hasCustomAudio: audioPath !== ""

    focus: true

    Keys.onPressed: function(event) {
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

    Connections {
        target: mpvController
        // Ambient clips loop, so in practice mpv only exits on a user quit; handle
        // the single playbackEnded signal regardless of reason — stop the companion
        // audio and return to the menu.
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            ambientModeBackend.stopAudio()
            goBack()
        }
    }

    Component.onCompleted: {
        if (videoPath === "") {
            goBack()
            return
        }
        mpvController.loadAndPlay(videoPath, 0.0, 0, -1, [], [], true, -1, 0.0, "", hasCustomAudio, "ambient")
        if (hasCustomAudio)
            ambientModeBackend.startAudio(audioPath)
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }
}
