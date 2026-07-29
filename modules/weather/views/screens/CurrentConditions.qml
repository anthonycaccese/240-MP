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

    readonly property real lineSize: root.sh * 0.05 //24
    readonly property real lineGap:  root.sh * 0.0138021 //6.625

    Text {
        id: title
        text: "CURRENTLY IN " + screen.locationName
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: screen.lineSize
        width: parent.width
        elide: Text.ElideRight
    }

    Text {
        id: conditions
        anchors.top: title.bottom
        anchors.topMargin: root.sh * 0.0854167 //41
        text: (screen.wx.condition || "") + " " + (screen.wx.temperature || "")
        color: root.accentColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0875 //42
        elide: Text.ElideRight
    }

    Column {
        anchors.top: conditions.bottom
        anchors.topMargin: root.sh * 0.0395833 //19
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: screen.lineGap

        Text {
            text: "HUMIDITY: " + (screen.wx.humidity || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
            elide: Text.ElideRight
        }
        Text {
            text: "DEWPOINT: " + (screen.wx.dewPoint || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
            elide: Text.ElideRight
        }
        Text {
            text: "PRESSURE: " + (screen.wx.pressure || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
            elide: Text.ElideRight
        }
        Text {
            text: "WIND: " + (screen.wx.wind || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
            elide: Text.ElideRight
        }
        Text {
            text: "VISIBILITY: " + (screen.wx.visibility || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
            elide: Text.ElideRight
        }
    }
}
