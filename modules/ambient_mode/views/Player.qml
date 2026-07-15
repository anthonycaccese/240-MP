import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string videoPath:      navParams.videoPath || ""
    property string audioPath:      navParams.audioPath || ""
    property bool   hasCustomAudio: audioPath !== ""
    property bool   shuffleVideo:   navParams.shuffleVideo || false
    property bool   shuffleAudio:   navParams.shuffleAudio || false
    property var    videoPaths:     navParams.videoPaths || []
    property var    audioPaths:     navParams.audioPaths || []

    focus: true

    // Fisher-Yates. mpv's own --shuffle reshuffles the *entire* playlist it's
    // given, including whatever was passed as the first entry, so a manually
    // picked item is not guaranteed to play first once --shuffle is set. Instead
    // we shuffle everything but the picked item ourselves and hand mpv a fixed
    // order (no --shuffle), which keeps the pick first and randomizes the rest.
    function shuffled(list) {
        var arr = list.slice()
        for (var i = arr.length - 1; i > 0; i--) {
            var j = Math.floor(Math.random() * (i + 1))
            var tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp
        }
        return arr
    }

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

        // Shuffle wins: hand mpv the rest of the library, pre-shuffled by us, as
        // extra playlist entries and let --loop-playlist=inf cycle through it on
        // its own, rather than the app detecting each clip's end and reloading a
        // new one. videoPath stays first in the list so the pick you made (or the
        // random start auto-launch chose) actually plays first.
        var otherVideos = (shuffleVideo && videoPaths.length > 1)
            ? shuffled(videoPaths.filter(function(p) { return p !== videoPath }))
            : []
        mpvController.loadAndPlay(videoPath, 0.0, 0, -1, [], [], true, -1, 0.0, "", hasCustomAudio, "ambient", false, [], 0.0, false, otherVideos)

        if (hasCustomAudio) {
            if (shuffleAudio && audioPaths.length > 1) {
                var otherAudio = shuffled(audioPaths.filter(function(p) { return p !== audioPath }))
                ambientModeBackend.startAudio([audioPath].concat(otherAudio), false)
            } else {
                ambientModeBackend.startAudio([audioPath], false)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }
}
