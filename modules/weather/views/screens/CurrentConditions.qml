import QtQuick

// One WeatherStar screen: a left-aligned column of fixed text lines.
//
// Every value arrives from the backend already formatted for the active Units
// setting, so this file is purely layout. Fields are limited to what Open-Meteo
// actually reports — no derived or estimated values.
Item {
    id: screen

    // NOT named `data` — that is Item's default property in QML and
    // declaring it would shadow the children list.
    property var    wx: ({})
    property string locationName: ""

    readonly property real lineSize: root.sh * 0.075
    readonly property real lineGap:  root.sh * 0.018

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: screen.lineGap

        Text {
            text: "CONDITIONS AT " + screen.locationName
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
            width: parent.width
            elide: Text.ElideRight
        }
        Text {
            text: screen.wx.condition || ""
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
        }
        Text {
            text: "TEMPERATURE: " + (screen.wx.temperature || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
        }
        Text {
            text: "HUMIDITY: " + (screen.wx.humidity || "")
                  + "  DEWPOINT: " + (screen.wx.dewPoint || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
        }
        Text {
            text: "BAROMETRIC PRESSURE: " + (screen.wx.pressure || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
        }
        Text {
            text: "WIND: " + (screen.wx.wind || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
        }
        Text {
            text: "VISIB: " + (screen.wx.visibility || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
        }
    }
}
