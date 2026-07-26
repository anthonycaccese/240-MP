import QtQuick

// Sunrise/sunset for two days, then the next four moon phases.
//
// Two aligned blocks: a label column on the left with a value column per day,
// and a centred moon-phase table. Both use fixed fractions of the screen width
// rather than content-driven widths, so the columns stay put as the values
// change length through the day.
Item {
    id: screen

    property var almanac: ({})

    // Sized down from the other screens: this one carries the most rows (title,
    // three sun rows, a heading and four moon rows) and at 0.07 the last phase
    // overran the status bar.
    readonly property real lineSize: root.sh * 0.06
    readonly property real labelWidth: width * 0.30
    readonly property real dayWidth:   width * 0.28

    Text {
        id: title
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        text: "THE WEATHERSTAR ALMANAC"
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: screen.lineSize
    }

    // ── Sun ──────────────────────────────────────────────────────────────────
    Column {
        id: sun
        anchors.top: title.bottom
        anchors.topMargin: root.sh * 0.04
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: root.sh * 0.012

        // Day headings, offset past the label column.
        Row {
            Item { width: screen.labelWidth; height: 1 }
            Repeater {
                model: screen.almanac.days || []
                Text {
                    width: screen.dayWidth
                    text: modelData.name || ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Row {
            Text {
                width: screen.labelWidth
                text: "SUNRISE"
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: screen.lineSize
            }
            Repeater {
                model: screen.almanac.days || []
                Text {
                    width: screen.dayWidth
                    text: modelData.sunrise || ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Row {
            Text {
                width: screen.labelWidth
                text: "SUNSET"
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: screen.lineSize
            }
            Repeater {
                model: screen.almanac.days || []
                Text {
                    width: screen.dayWidth
                    text: modelData.sunset || ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    // ── Moon ─────────────────────────────────────────────────────────────────
    Text {
        id: moonTitle
        anchors.top: sun.bottom
        anchors.topMargin: root.sh * 0.035
        anchors.horizontalCenter: parent.horizontalCenter
        text: "MOON PHASES"
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: screen.lineSize
    }

    Column {
        anchors.top: moonTitle.bottom
        anchors.topMargin: root.sh * 0.015
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.sh * 0.012

        Repeater {
            model: screen.almanac.moons || []

            Row {
                spacing: root.sw * 0.03
                Text {
                    width: screen.width * 0.16
                    text: modelData.name || ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                    horizontalAlignment: Text.AlignRight
                }
                Text {
                    width: screen.width * 0.16
                    text: modelData.date || ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                }
            }
        }
    }
}
