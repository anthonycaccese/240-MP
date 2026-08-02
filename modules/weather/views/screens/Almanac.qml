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
    readonly property real lineSize: root.sh * 0.05 //24
    readonly property real labelWidth: width * 0.25
    readonly property real dayWidth:   width * 0.25

    Text {
        id: title
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        text: "ALMANAC"
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: screen.lineSize
    }

    // ── Sun ──────────────────────────────────────────────────────────────────
    Column {
        id: sun
        anchors.top: title.bottom
        anchors.topMargin: root.sh * 0.075
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: root.sh * 0.01

        // Day headings, offset past the label column.
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            Item { width: screen.labelWidth; height: 1 }
            Repeater {
                model: screen.almanac.days || []
                Text {
                    width: screen.dayWidth
                    text: modelData.name || ""
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            Text {
                width: screen.labelWidth
                text: "SUNRISE"
                color: root.accentColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0375
                horizontalAlignment: Text.AlignLeft
                anchors.verticalCenter: parent.verticalCenter
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
            anchors.horizontalCenter: parent.horizontalCenter
            Text {
                width: screen.labelWidth
                text: "SUNSET"
                color: root.accentColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0375
                horizontalAlignment: Text.AlignLeft
                anchors.verticalCenter: parent.verticalCenter
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

    Column {
        id: moon
        anchors.top: sun.bottom
        anchors.topMargin: root.sh * 0.0625
        anchors.left: parent.left
        anchors.right: parent.right

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
        
            Text {
                width: screen.labelWidth
                id: moonTitle
                text: "MOON PHASES"
                color: root.accentColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0375
                horizontalAlignment: Text.AlignLeft
            }

            Column {
                spacing: root.sh * 0.01
                Repeater {
                    model: screen.almanac.moons || []

                    Row {
                        Text {
                            width: screen.dayWidth
                            text: modelData.name || ""
                            color: root.primaryColor
                            font.family: root.globalFont
                            font.pixelSize: screen.lineSize
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            width: screen.dayWidth
                            text: modelData.date || ""
                            color: root.primaryColor
                            font.family: root.globalFont
                            font.pixelSize: screen.lineSize
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }

        }

        
    }
}
