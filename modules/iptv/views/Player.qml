import QtQuick

// Live playback: hand the stream URL straight to mpv. No resume, seek, or
// progress reporting — this is a live feed, not VOD.
FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string streamUrl: navParams.streamUrl || ""
    property string channelTitle: navParams.title || ""
    property string nowTitle: navParams.nowTitle || ""
    property string nextTitle: navParams.nextTitle || ""
    property bool playbackStarted: false
    // Info bar is shown on tune-in and for a few seconds after playback starts.
    property bool infoVisible: true

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

    // Hide the info bar a few seconds after playback actually starts.
    Timer {
        id: infoTimer
        interval: 5000
        repeat: false
        onTriggered: playerRoot.infoVisible = false
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0 && !playerRoot.playbackStarted) {
                playerRoot.playbackStarted = true
                infoTimer.restart()
            }
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
        // Only covers the screen before mpv takes over; once playback starts mpv
        // paints over this, and only the info bar (a separate overlay) shows.
        visible: !playerRoot.playbackStarted

        Text {
            text: "TUNING IN..."
            color: "white"
            font.family: root.globalFont
            anchors.centerIn: parent
            font.pixelSize: root.sh * 0.05
            visible: playerRoot.streamUrl !== ""
        }
    }

    // Channel / now-next info bar — shown on tune-in and briefly after start.
    Rectangle {
        id: infoBar
        visible: playerRoot.infoVisible && playerRoot.channelTitle !== ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: infoColumn.implicitHeight + root.sh * 0.05
        color: Qt.rgba(0, 0, 0, 0.6)

        Column {
            id: infoColumn
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: root.sw * 0.05
            anchors.right: parent.right
            anchors.rightMargin: root.sw * 0.05
            spacing: root.sh * 0.008

            Text {
                text: playerRoot.channelTitle
                color: "white"
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.045
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                visible: playerRoot.nowTitle !== ""
                text: "NOW · " + playerRoot.nowTitle
                color: root.accentColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.03
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                visible: playerRoot.nextTitle !== ""
                text: "NEXT · " + playerRoot.nextTitle
                color: "#cccccc"
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.03
                elide: Text.ElideRight
                width: parent.width
            }
        }
    }
}
