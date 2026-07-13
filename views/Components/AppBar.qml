import QtQuick
import QtQuick.Effects

Row {
    id: appBar
    
    // Custom Properties
    property url iconSource: "../../assets/images/logo.svg"
    property string title: "240-MP"
    property string subtitle: ""

    // Fits the standard screen gutter — 80px (root.sw * 0.125) on each side.
    // The subtitle elides when it would overflow this width.
    width: root.sw * 0.75 //480

    spacing: root.sw * 0.025 //16
    Item {
        visible: appBar.iconSource !== ""
        width: iconImg.width
        anchors.verticalCenter: parent.verticalCenter
        height: root.sh * 0.05 //24
        Image {
            visible: false
            id: iconImg
            height: parent.height
            sourceSize.height: height
            source: appBar.iconSource
        }
        MultiEffect {
            anchors.fill: iconImg
            source: iconImg
            colorization: 1.0
            colorizationColor: root.accentColor
        }
    }

    Text {
        text: appBar.title
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: root.sh * 0.05 //24
    }

    Rectangle {
        visible: appBar.subtitle !== ""
        color: root.secondaryColor
        anchors.verticalCenter: parent.verticalCenter
        width: root.sw * 0.0015625 //1
        height: root.sh * 0.05 //24
    }

    Text {
        text: appBar.subtitle
        color: root.secondaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: root.sh * 0.0333333 //16
        // x is this Text's Row position, i.e. everything before it (icon,
        // title, separator, spacings) — cap to the bar's remaining space.
        elide: Text.ElideRight
        width: Math.max(0, Math.min(implicitWidth, appBar.width - x))
    }
}
