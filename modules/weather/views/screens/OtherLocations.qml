import QtQuick

// Current conditions for the extra places listed in weather_location.txt.
// Open-Meteo has no station data and no "places near me" lookup, so this is a user-chosen
// list instead so it allows the places can be local, regional or on the other side of the world.
Item {
    id: screen

    property var rows: []
    property string tempUnit: "°F"

    readonly property real lineSize: root.sh * 0.05 //24
    readonly property real headerSize: root.sh * 0.0333333 //16

    // Fixed fractions rather than content-driven widths, so the columns stay put
    // as values change length through the day.
    readonly property real nameWidth: width * 0.4
    readonly property real wxWidth:   width * 0.4375
    readonly property real tempWidth: width * 0.125

    Text {
        id: title
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        text: "OTHER LOCATIONS"
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: screen.lineSize
    }

    Row {
        id: header
        anchors.top: title.bottom
        anchors.topMargin: root.sh * 0.0854167 //41
        anchors.left: parent.left
        anchors.right: parent.right
        height: screen.lineSize

        Text {
            width: screen.nameWidth; text: "LOCATION"
            color: root.accentColor
            font.family: root.globalFont
            font.pixelSize: screen.headerSize
        }
        Item { width: root.sw * 0.0140625; height: 1 }
        Text {
            width: screen.wxWidth; text: "CONDITIONS"
            color: root.accentColor
            font.family: root.globalFont
            font.pixelSize: screen.headerSize
        }
        Item { width: root.sw * 0.0140625; height: 1 }
        Text {
            width: screen.tempWidth; text: screen.tempUnit
            color: root.accentColor
            font.family: root.globalFont
            font.pixelSize: screen.headerSize
            horizontalAlignment: Text.AlignRight
        }
    }

    Column {
        anchors.top: header.bottom
        anchors.topMargin: root.sh * 0.0085 // 4.08
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: root.sh * 0.0138021 //6.625

        Repeater {
            model: screen.rows

            Row {
                Text {
                    width: screen.nameWidth
                    // Long place names are truncated rather than allowed to push
                    // the numeric columns out of alignment.
                    text: modelData.name || ""
                    elide: Text.ElideRight
                    color: root.primaryColor; font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                }
                Item { width: root.sw * 0.0140625; height: 1 }
                Text {
                    width: screen.wxWidth; text: modelData.condition || ""
                    elide: Text.ElideRight
                    color: root.primaryColor; font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                }
                Item { width: root.sw * 0.0140625; height: 1 }
                Text {
                    width: screen.tempWidth; text: modelData.temp || ""
                    color: root.primaryColor; font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }
}
