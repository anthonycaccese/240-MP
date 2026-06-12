import QtQuick
import Components

FocusScope {
    id: pinAuthRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal replaceWith(string path, var params)
    signal goBack()

    property string pinCode: ""
    property bool waiting: false
    property string errorMsg: ""

    Connections {
        target: plexBackend

        function onPinReady(code, pinId) {
            pinAuthRoot.pinCode = code
            pinAuthRoot.waiting = true
            pinAuthRoot.errorMsg = ""
        }

        function onAuthSuccess() {
            // After PIN claimed: users + servers fetched, move to user selection
        }

        function onUsersLoaded(users) {
            if (users.length === 1) {
                // Only one user — auto-select and go straight to server select
                plexBackend.select_user(users[0].id)
            } else {
                pinAuthRoot.replaceWith("UserSelect.qml", { users: users })
            }
        }

        function onServersLoaded(servers) {
            pinAuthRoot.replaceWith("ServerSelect.qml", { servers: servers })
        }

        function onErrorOccurred(msg) {
            pinAuthRoot.waiting = false
            pinAuthRoot.errorMsg = msg
        }
    }

    Component.onCompleted: {
        plexBackend.start_pin_auth()
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Sign in"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Body
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.05 //24

        Text {
            visible: pinCode !== ""
            text: pinCode
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.1166667 //56
        }

        Text {
            visible: pinCode !== ""
            text: "Visit plex.tv/link and enter the code above"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }

        // Waiting indicator with animated dots
        Row {
            visible: waiting && pinCode !== ""
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                text: "Waiting"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0333333 //16
            }

            Text {
                id: dots
                text: "..."
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333 //16

                SequentialAnimation on opacity {
                    running: waiting
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }
        }

        // Loading Indicator
        Text {
            visible: pinCode === "" && errorMsg === ""
            text: "Requesting Pin..."
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0416667 //20
        }

        // Error message
        Text {
            visible: errorMsg !== ""
            text: errorMsg
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0375 //20
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
