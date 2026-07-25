import QtQuick
import Components

// Emby Connect server picker: shown when an account is linked to more than one
// server. Selecting a server exchanges its access key for a local token
// (embyBackend.connect_select_server); Root.qml routes to Libraries on success.
FocusScope {
    id: pickerRoot
    focus: true

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var servers: navParams.servers || []
    property bool waiting: false
    property string errorMsg: ""

    Connections {
        target: embyBackend

        // A failed exchange returns here so the user can pick another server.
        function onConnectFailed(msg) {
            pickerRoot.waiting = false
            pickerRoot.errorMsg = msg
        }
    }

    function selectCurrent() {
        if (waiting) return
        var s = servers[serverList.currentIndex]
        if (!s) return
        waiting = true
        errorMsg = ""
        embyBackend.connect_select_server(s.address, s.accessKey)
    }

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Choose a Server"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Connecting indicator
    Text {
        visible: pickerRoot.waiting
        text: "CONNECTING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    ListView {
        id: serverList
        model: servers
        visible: !pickerRoot.waiting
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: if (currentIndex > 0) currentIndex--
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++
        Keys.onReturnPressed: pickerRoot.selectCurrent()
        Keys.onEnterPressed: pickerRoot.selectCurrent()

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                pickerRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: serverList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, serverList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: serverList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.name || modelData.address || ""
                    color: serverList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
                }

                SequentialAnimation {
                    running: (serverList.currentIndex === index) &&
                             (rowText.implicitWidth > textClip.width)
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) rowText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: rowText; property: "x"
                        to: textClip.width - rowText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: rowText; property: "x"; value: 0 }
                }
            }
        }
    }

    // Error message
    Text {
        visible: errorMsg !== "" && !pickerRoot.waiting
        text: errorMsg
        color: root.accentColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: footer.top
        anchors.bottomMargin: root.sh * 0.0333333 //16
        width: root.sw * 0.5
        wrapMode: Text.WordWrap
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
