import QtQuick
import QtQuick.Effects
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string errorMessage: ""

    focus: true

    Component.onCompleted: {
        errorMessage = nfcReaderBackend.mappingLoaded ? "" : "REQUIRED FILES NOT FOUND\n\n"
                             + "ADD NFC_MAPPING.JSON\n\n"
                             + "PLEASE SEE THE WIKI FOR DETAILS"
    }

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
        visible: errorMessage === ""
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
                    colorizationColor: nfcReaderBackend.cardState === "matched" ? root.accentColor : root.primaryColor
                    opacity: !nfcReaderBackend.readerConnected ? 0.2
                        : nfcReaderBackend.cardState === "matched" ? 0.8
                        : nfcReaderBackend.cardState === "unmatched" ? 0.2
                        : 0.5
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
                    text: !nfcReaderBackend.readerConnected ? "Reader not connected"
                        : nfcReaderBackend.cardState === "matched" ? "Playing \u25BA"
                        : nfcReaderBackend.cardState === "unmatched" ? "Card not matched"
                        : "Tap a card to play"
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
                visible: nfcReaderBackend.readerConnected && nfcReaderBackend.cardState !== "none"
                text: nfcReaderBackend.cardState === "matched" ? nfcReaderBackend.videoTitle : nfcReaderBackend.cardUid
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
        visible: errorMessage !== ""
        text: errorMessage
        color: root.secondaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        width: root.sw * 0.76875
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05
    }

    Connections {
        target: nfcReaderBackend
        function onMappingLoadedChanged() {
            errorMessage = nfcReaderBackend.mappingLoaded ? "" : "REQUIRED FILES NOT FOUND\n\n"
                             + "ADD NFC_MAPPING.JSON\n\n"
                             + "PLEASE SEE THE WIKI FOR DETAILS"
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