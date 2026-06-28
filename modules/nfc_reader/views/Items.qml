import QtQuick
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

    Connections {
        target: nfcReaderBackend
        function onStatusChanged(status) {
            statusText.text = status
        }
        function onErrorOccurred(message) {
            errorText.text = message
            errorText.visible = true
        }
        function onReaderReadyChanged() {
            statusText.text = nfcReaderBackend.readerReady ? "READY FOR NFC CARD" : "READER NOT FOUND"
            statusIndicator.border.color = nfcReaderBackend.readerReady ? root.accentColor : root.tertiaryColor
            statusText.color = nfcReaderBackend.readerReady ? root.accentColor : root.tertiaryColor
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

    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333

        Text {
            id: titleText
            text: "NFC CARD READER"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.06
        }

        Rectangle {
            id: statusIndicator
            width: root.sw * 0.3
            height: root.sh * 0.15
            radius: root.sh * 0.015
            border.color: nfcReaderBackend.readerReady ? root.accentColor : root.tertiaryColor
            border.width: root.sh * 0.003125

            Text {
                id: statusText
                text: nfcReaderBackend.readerReady ? "READY FOR NFC CARD" : "READER NOT FOUND"
                color: nfcReaderBackend.readerReady ? root.accentColor : root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.centerIn: parent
                font.pixelSize: root.sh * 0.035
            }
        }

        Text {
            id: instructionText
            text: "Present an NFC card to play video"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333
        }

        Text {
            id: errorText
            text: ""
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.025
            visible: false
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
