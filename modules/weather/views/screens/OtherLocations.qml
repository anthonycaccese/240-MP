import QtQuick

// Current conditions for the extra places listed in weather_location.txt.
//
// The 1980s original showed nearby airport observation stations here. Open-Meteo
// has no station data and no "places near me" lookup, so this is a user-chosen
// list instead — which also travels better: the places can be local, regional or
// on the other side of the world.
Item {
    id: screen

    property var rows: []
    property string tempUnit: "°F"

    readonly property real lineSize:   root.sh * 0.06
    readonly property real headerSize: root.sh * 0.042

    // Fixed fractions rather than content-driven widths, so the columns stay put
    // as values change length through the day.
    readonly property real nameWidth: width * 0.46
    readonly property real tempWidth: width * 0.12
    readonly property real wxWidth:   width * 0.24

    Text {
        id: title
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        text: "OTHER LOCATIONS"
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.07
    }

    Row {
        id: header
        anchors.top: title.bottom
        anchors.topMargin: root.sh * 0.035
        anchors.left: parent.left
        anchors.right: parent.right

        Text {
            width: screen.nameWidth; text: "LOCATION"
            color: root.secondaryColor; font.family: root.globalFont
            font.pixelSize: screen.headerSize
        }
        Text {
            width: screen.tempWidth; text: screen.tempUnit
            color: root.secondaryColor; font.family: root.globalFont
            font.pixelSize: screen.headerSize
            horizontalAlignment: Text.AlignRight
        }
        Item { width: root.sw * 0.02; height: 1 }
        Text {
            width: screen.wxWidth; text: "WEATHER"
            color: root.secondaryColor; font.family: root.globalFont
            font.pixelSize: screen.headerSize
        }
        Text {
            text: "WIND"
            color: root.secondaryColor; font.family: root.globalFont
            font.pixelSize: screen.headerSize
        }
    }

    Column {
        anchors.top: header.bottom
        anchors.topMargin: root.sh * 0.015
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: root.sh * 0.008

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
                Text {
                    width: screen.tempWidth; text: modelData.temp || ""
                    color: root.primaryColor; font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                    horizontalAlignment: Text.AlignRight
                }
                Item { width: root.sw * 0.02; height: 1 }
                Text {
                    width: screen.wxWidth; text: modelData.condition || ""
                    elide: Text.ElideRight
                    color: root.primaryColor; font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                }
                Text {
                    text: modelData.wind || ""
                    color: root.primaryColor; font.family: root.globalFont
                    font.pixelSize: screen.lineSize
                }
            }
        }
    }
}
