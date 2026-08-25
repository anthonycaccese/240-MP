import QtQuick

// A Text that slowly scrolls left-to-right when it's too wide for its own
// width to reveal the rest, instead of eliding it — same pattern as
// views/ModuleList.qml's long-module-name marquee (pause, scroll at a
// constant 20ms/pixel rate, pause, snap back), just packaged as a reusable
// component since AirPlay's Now Playing screen needs it on four separate
// lines (title/artist/album/sender) rather than the one ModuleList has.
Item {
    id: marquee

    property alias text: label.text
    property alias color: label.color
    property alias font: label.font
    property alias horizontalAlignment: label.horizontalAlignment

    implicitHeight: label.implicitHeight
    clip: true

    Text {
        id: label
        x: 0
        elide: Text.ElideNone
        wrapMode: Text.NoWrap
    }

    SequentialAnimation {
        running: label.implicitWidth > marquee.width
        loops: Animation.Infinite

        onRunningChanged: if (!running) label.x = 0

        PauseAnimation {
            duration: 1500
        }
        NumberAnimation {
            target: label
            property: "x"
            to: marquee.width - label.implicitWidth
            duration: Math.abs(to) * 20
        }
        PauseAnimation {
            duration: 2000
        }
        PropertyAction {
            target: label
            property: "x"
            value: 0
        }
    }
}
