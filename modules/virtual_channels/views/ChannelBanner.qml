import QtQuick

// The channel number and name, top right, drawn by the app.
//
// mpv draws its own once a picture is up, and has to: during playback it owns
// the screen and anything drawn here would sit behind the video unseen. The
// wait before that is the other half, and on a fast surf it is most of it --
// mpv is stopped, so this is the only thing that can say where the dial has
// got to. Geometry matches scripts/mpv-osd-channel.lua so the two read as one
// banner that simply stays put while the picture catches up.
Item {
    id: channelBanner

    property string number: ""
    property string channel: ""
    property real offsetX: 0
    property real offsetY: 0
    property real seconds: 1.5
    property color textColor: "white"
    property string fontFamily: ""

    opacity: 0
    visible: opacity > 0

    function show(bannerNumber, bannerName) {
        number = bannerNumber
        channel = bannerName
        opacity = 1
        hideTimer.restart()
    }

    function hideNow() {
        hideTimer.stop()
        opacity = 0
    }

    Timer {
        id: hideTimer
        interval: Math.max(500, channelBanner.seconds * 1000)
        onTriggered: channelBanner.opacity = 0
    }

    Behavior on opacity { NumberAnimation { duration: 120 } }

    Column {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: parent.width * 0.06 - channelBanner.offsetX * parent.width
        anchors.topMargin: parent.height * 0.06 + channelBanner.offsetY * parent.height
        spacing: parent.height * 0.01

        Item {
            width: numberText.width
            height: channelBanner.number === "" ? 0 : numberText.height
            visible: channelBanner.number !== ""
            anchors.right: parent.right

            Text {
                x: parent.width * 0 + 2
                y: 2
                text: numberText.text
                color: "black"
                font: numberText.font
            }
            Text {
                id: numberText
                text: channelBanner.number
                color: channelBanner.textColor
                font.family: channelBanner.fontFamily
                font.pixelSize: channelBanner.height * 0.11
            }
        }

        Item {
            width: nameText.width
            height: channelBanner.channel === "" ? 0 : nameText.height
            visible: channelBanner.channel !== ""
            anchors.right: parent.right

            Text {
                x: 2
                y: 2
                text: nameText.text
                color: "black"
                font: nameText.font
            }
            Text {
                id: nameText
                text: channelBanner.channel.toUpperCase()
                color: channelBanner.textColor
                font.family: channelBanner.fontFamily
                font.pixelSize: channelBanner.height * 0.045
            }
        }
    }
}
