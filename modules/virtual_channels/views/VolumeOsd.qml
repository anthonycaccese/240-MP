import QtQuick

// The volume bar, and the keys that drive it, for the screens mpv is not on.
//
// Volume keys are routed straight to mpv, which no-ops when mpv is not running
// -- so the guide, the weather channel and the card between programmes had no
// volume at all. They do now, and this is the same bar mpv draws
// (scripts/mpv-media-keys.lua), laid out from the same fractions of the screen
// so that turning the volume up on the guide and turning it up over a programme
// look like one control rather than two.
Item {
    id: osd

    property string moduleId: ""
    // False while mpv is playing: it draws this bar itself and holds its own
    // volume, so a second one here would move the number twice per press.
    property bool active: true

    property int  level: 100
    property bool muted: false

    // The host puts `level` wherever the sound is actually coming from, which
    // is a different thing on every screen that uses this.
    signal adjusted()

    readonly property int step: 5
    readonly property int maxLevel: 100

    function bump(delta) {
        var next = Math.max(0, Math.min(maxLevel, level + delta))
        // Turning it up is also how you turn the mute off, which is what the
        // button on any amplifier does.
        if (muted && delta > 0) muted = false
        if (next !== level) {
            level = next
            if (moduleId !== "") appCore.save_setting(moduleId, "volume", String(level))
        }
        show()
        adjusted()
    }

    function toggleMute() {
        muted = !muted
        show()
        adjusted()
    }

    function show() {
        hideTimer.restart()
        opacity = 1
    }

    Connections {
        target: inputManager
        function onMpvKeyRequested(key) {
            if (!osd.active) return
            if (key === "VOLUME_UP")        osd.bump(osd.step)
            else if (key === "VOLUME_DOWN") osd.bump(-osd.step)
            else if (key === "MUTE")        osd.toggleMute()
        }
    }

    Timer {
        id: hideTimer
        interval: 1500
        onTriggered: osd.opacity = 0
    }

    anchors.fill: parent
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    // Every measurement below is the fraction mpv's own bar uses, so the two
    // land in the same place on the same screen.
    readonly property real fs:    root.sh * 0.0333333
    readonly property real lm:    root.sw * 0.12
    readonly property real barW:  root.sw * 0.88 - lm
    readonly property real barH:  fs * 2
    readonly property int  ticks: Math.max(1, Math.round(maxLevel / step))
    readonly property real slotW: barW / ticks
    readonly property real gap:   Math.max(1, Math.floor(slotW * 0.35))
    readonly property real tickW: Math.max(1, Math.floor(slotW - gap))
    readonly property real dashH: Math.max(2, Math.floor(barH * 0.15))

    // Bottom-anchored so the large label grows upward off the bar row instead of
    // over the ticks. Says MUTE when muted, because a mute leaves the ticks
    // where they were and the bar would otherwise say nothing had happened.
    Text {
        x: osd.lm
        y: root.sh * 0.7979166 - height
        text: osd.muted ? "MUTE" : "VOLUME"
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: osd.fs * 3
    }

    Row {
        x: osd.lm
        y: root.sh * 0.8333333
        spacing: osd.slotW - osd.tickW

        Repeater {
            model: osd.ticks
            delegate: Item {
                required property int index
                width: osd.tickW
                height: osd.barH

                // A filled tick is the full height of the row; an empty one is a
                // dash centred in the same slot, so the row does not change
                // width as the volume moves.
                readonly property bool filled:
                    !osd.muted && index < Math.round(osd.level / osd.step)

                Rectangle {
                    width: parent.width
                    height: parent.filled ? osd.barH : osd.dashH
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.primaryColor
                    opacity: parent.filled ? 1.0 : 0.31
                }
            }
        }
    }
}
