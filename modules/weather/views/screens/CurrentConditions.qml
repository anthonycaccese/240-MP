import QtQuick
import QtQuick.Effects

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
        // Stops short of the icon rather than the screen edge, so a long
        // location name elides instead of running underneath it.
        width: parent.width - icon.width - (root.sw * 0.02)
        elide: Text.ElideRight
    }

    // Anchored to the right edge rather than following the title, so its
    // position doesn't move with the length of the location name.
    //
    // Tinted at runtime rather than shipped coloured — the app has a custom
    // colour scheme feature, and a baked-in colour would be wrong the moment
    // anyone changes theme. Same hidden-Image + MultiEffect pattern as
    // nfc_reader/views/Items.qml.
    Item {
        id: icon
        anchors.right: parent.right
        anchors.verticalCenter: title.verticalCenter
        width:  iconImage.width
        height: root.sh * 0.11
        // An unmapped code gives an empty name; draw nothing rather than a
        // broken-image box.
        visible: iconImage.source != ""

        Image {
            id: iconImage
            visible: false
            height: parent.height
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            source: screen.wx.iconName
                ? "../../assets/images/wx/" + screen.wx.iconName + ".svg"
                : ""
        }
        MultiEffect {
            anchors.fill: iconImage
            source: iconImage
            colorization: 1.0
            colorizationColor: root.accentColor
        }
    }

    Text {
        id: conditions
        anchors.top: title.bottom
        anchors.topMargin: root.sh * 0.0854167 //41
        text: (screen.wx.condition || "") + " " + (screen.wx.temperature || "")
        color: root.accentColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0875 //42
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
        }
        Text {
            text: "DEWPOINT: " + (screen.wx.dewPoint || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
        }
        Text {
            text: "PRESSURE: " + (screen.wx.pressure || "")
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
            text: "VISIBILITY: " + (screen.wx.visibility || "")
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: screen.lineSize
        }
    }
}
