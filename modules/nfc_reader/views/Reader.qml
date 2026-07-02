import QtQuick
import QtQuick.Effects
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    Item {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.2604167 //125
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252

        Item {
            anchors.centerIn: parent
            height: statusIndicator.height

            Item {
                id: statusIndicator
                anchors.horizontalCenter: parent.horizontalCenter
                width: statusIndicatorImage.width
                height: root.sh * 0.3791667 //182
                Image {
                    visible: false
                    id: statusIndicatorImage
                    height: parent.height
                    sourceSize.height: height
                    source: "../assets/images/vhs.svg"
                }
                MultiEffect {
                    id: statusIndicatorColor
                    anchors.fill: statusIndicatorImage
                    source: statusIndicatorImage
                    colorization: 1.0
                    colorizationColor: root.accentColor // root.accentColor or root.primaryColor or root.primaryColor or root.primaryColor
                    opacity: 0.5 // 1 or 0.2 or 0.2 or 0.5
                }
            }

            Rectangle {
                id: statusLabel
                anchors.centerIn: parent
                color: root.surfaceColor
                width: statusIndicatorImage.width * 0.365
                height: statusIndicator.height * 0.375
                clip: true
                Text {
                    id: statusText
                    text: "Tap a card to play" // "Playing \u25BA" or "Card not matched" or "Reader not connected" or "Tap a card to play"
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    width: parent.width * 0.9
                    height: parent.height * 0.9
                    anchors.centerIn: parent
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: root.sh * 0.0333333 //16
                    wrapMode: Text.WordWrap
                    lineHeight: 1.3
                }
            }

            Text {
                id: additionalText
                visible: false
                text: "0F:25:14:S4"
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root.sh * 0.05 //24
                font.pixelSize: root.sh * 0.0291667 //14
            }
        }
    }

    Text {
        id: footer
        text: root.hints.back + ":BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}