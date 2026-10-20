import QtQuick
import Components

// Emby Connect PIN sign-in: shows a PIN the user enters at emby.media/pin
// (signed in to their Connect account), then polls until it is confirmed.
// Confirmation continues as the normal Connect flow — a server picker if the
// account has several servers, otherwise Root.qml routes to Libraries on
// authStateChanged.
FocusScope {
    id: pinRoot
    focus: true

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal replaceCurrent(string path, var params)
    signal goBack()

    property string pin: ""
    property bool failed: false
    property string errorMsg: ""
    property int pollCount: 0

    Connections {
        target: embyBackend

        function onConnectPinReady(p) {
            pinRoot.pin = p
            pinRoot.pollCount = 0
            pollTimer.restart()
        }

        // Reaching the server list means the PIN was confirmed and exchanged.
        // Swap in the picker in place so BACK from it returns to Auth rather
        // than to a spent PIN.
        function onConnectServersReady(servers) {
            pollTimer.stop()
            pinRoot.replaceCurrent("ConnectServers.qml", { servers: servers })
        }

        function onConnectFailed(msg) {
            pollTimer.stop()
            pinRoot.failed = true
            pinRoot.errorMsg = msg
        }
    }

    function start() {
        pin = ""
        failed = false
        errorMsg = ""
        embyBackend.connect_pin_create()
    }

    Component.onCompleted: start()

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace ||
            event.key === Qt.Key_Back) {
            pollTimer.stop()
            embyBackend.connect_pin_cancel()
            goBack()
            event.accepted = true
        } else if (failed && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            start()
            event.accepted = true
        }
    }

    // Poll every 3 seconds. The service reports expiry itself (IsExpired); the
    // local cap only guards against a service that never answers definitively.
    Timer {
        id: pollTimer
        interval: 3000
        repeat: true
        onTriggered: {
            if (pinRoot.pin === "" || pinRoot.failed) return
            pinRoot.pollCount++
            if (pinRoot.pollCount > 200) { // 10 minutes
                stop()
                embyBackend.connect_pin_cancel()
                pinRoot.failed = true
                pinRoot.errorMsg = "PIN EXPIRED"
                return
            }
            embyBackend.connect_pin_poll()
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Sign In with PIN"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.05 //24
        visible: !pinRoot.failed

        Text {
            visible: pinRoot.pin !== ""
            text: pinRoot.pin
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.1166667 //56
            font.letterSpacing: root.sw * 0.025 //16
        }

        Text {
            visible: pinRoot.pin !== ""
            text: "Enter the above PIN at:\nemby.media/pin"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
            lineHeight: 1.3
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            Text {
                text: pinRoot.pin !== "" ? "Waiting" : "Connecting"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0333333 //16
            }
            Text {
                text: "..."
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333 //16
                SequentialAnimation on opacity {
                    running: !pinRoot.failed
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }
        }
    }

    // Error state
    Text {
        visible: pinRoot.failed
        anchors.centerIn: parent
        width: root.sw * 0.75
        text: pinRoot.errorMsg
        color: root.accentColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pixelSize: root.sh * 0.05 //24
    }

    // Footer
    Text {
        text: pinRoot.failed ? root.hints.back + ":BACK " + root.hints.select + ":NEW PIN"
                             : root.hints.back + ":CANCEL"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
