import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    // A single picked file, or a non-empty *Paths list when the picker's SHUFFLE
    // slot was chosen for that row — never both.
    property string videoPath:      navParams.videoPath || ""
    property string audioPath:      navParams.audioPath || ""
    property var    videoPaths:     navParams.videoPaths || []
    property var    audioPaths:     navParams.audioPaths || []
    property bool   shuffleVideo:   videoPaths.length > 0
    property bool   shuffleAudio:   audioPaths.length > 0
    property bool   hasCustomAudio: audioPath !== "" || audioPaths.length > 0

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
        if (videoPath === "" && videoPaths.length === 0) {
            goBack()
            return
        }

        // Shuffling hands mpv the whole library as one playlist and lets --shuffle
        // plus --loop-playlist=inf (from loop: true) cycle it, rather than the app
        // detecting each clip's end and loading a new one. First entry goes in as
        // the url, the rest as extra playlist entries.
        var first = shuffleVideo ? videoPaths[0] : videoPath
        var rest  = shuffleVideo ? videoPaths.slice(1) : []
        mpvController.loadAndPlay(first, 0.0, 0, -1, [], [], true, -1, 0.0, "",
                                  hasCustomAudio, "ambient", shuffleVideo, [], 0.0,
                                  false, [], "", rest)

        // Companion audio is a second, independent mpv process — it does its own
        // shuffling and looping (see AmbientModeBackend::startAudio).
        if (audioPaths.length > 0)
            ambientModeBackend.startAudio(audioPaths, shuffleAudio)
        else if (audioPath !== "")
            ambientModeBackend.startAudio([audioPath], false)
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }
}
