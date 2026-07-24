import QtQuick
import Components

FocusScope {
    focus: true
    id: authRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string serverUrl: appCore.get_setting(moduleRoot.moduleId, "server_url") || ""
    property string username: ""
    property string password: ""
    property bool waiting: false
    property string errorMsg: ""

    // Focus index: 0=server URL, 1=username, 2=password, 3=Sign In, 4=Emby Connect
    property int focusIndex: 0

    Connections {
        target: embyBackend

        function onAuthStateChanged() {
            // Root.qml owns post-auth routing and server_url persistence (the single
            // funnel for both direct and Emby Connect sign-in); here we just drop the
            // connecting spinner.
            if (embyBackend.get_auth_state() === "authed")
                authRoot.waiting = false
        }

        function onErrorOccurred(msg) {
            authRoot.waiting = false
            authRoot.errorMsg = msg
        }

        // Emby Connect: more than one server on the account — let the user pick.
        function onConnectServersReady(servers) {
            authRoot.waiting = false
            authRoot.navigateTo("ConnectServers.qml", { servers: servers }, {})
        }

        function onConnectFailed(msg) {
            authRoot.waiting = false
            authRoot.errorMsg = msg
        }
    }

    Component.onCompleted: {
        focusIndex = 0
    }

    // Navigation keys — don't intercept modifier keys so Shift/CapsLock work.
    // Backspace is deliberately NOT a BACK shortcut here (unlike the browse
    // views) so it deletes characters while a text field is focused; ESC/Back
    // leaves the auth screen.
    Keys.onPressed: function(event) {
        // Never intercept bare modifier keys
        if (event.key === Qt.Key_Shift || event.key === Qt.Key_Control ||
            event.key === Qt.Key_Alt   || event.key === Qt.Key_Meta ||
            event.key === Qt.Key_AltGr) {
            return
        }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
            return
        }
        if (waiting) {
            event.accepted = true
            return
        }
        // Every field/button is one linear sequence (Server URL, Username,
        // Password, Sign In, Emby Connect) even though some sit side by side.
        // Down/Right/Tab advance through it and Up/Left/Shift+Tab retreat,
        // wrapping around at the ends — so all of them behave like Tab. (This
        // means Left/Right no longer move the text cursor within a field.)
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Right
            || event.key === Qt.Key_Tab) {
            focusIndex = (focusIndex + 1) % 5
            event.accepted = true
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left
                   || event.key === Qt.Key_Backtab) { // Backtab = Shift+Tab
            focusIndex = (focusIndex + 4) % 5
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (focusIndex === 4) authRoot.submitConnect()
            else authRoot.submit()
            event.accepted = true
        }
    }

    // Direct sign-in against a single server (server URL + username + password).
    function submit() {
        if (waiting) return
        if (serverUrl === "") {
            errorMsg = "Please enter a server URL"
            return
        }
        if (username === "") {
            errorMsg = "Please enter a username"
            return
        }
        waiting = true
        errorMsg = ""
        embyBackend.authenticate(authRoot.serverUrl, authRoot.username, authRoot.password)
    }

    // Emby Connect: cloud sign-in with the emby.media account (username/email +
    // password). The server URL field is ignored — servers are discovered from
    // the account, and a picker appears if there is more than one.
    function submitConnect() {
        if (waiting) return
        if (username === "") {
            errorMsg = "Enter your Emby Connect username or email"
            return
        }
        waiting = true
        errorMsg = ""
        embyBackend.connect_authenticate(authRoot.username, authRoot.password)
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Connect to Server"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Column {
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.2791667 //134
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.sh * 0.075 //36

        // Reusable labelled text field. Up/Down navigation and Enter/Escape are
        // handled by authRoot.Keys — each TextInput forwards keys up by not
        // accepting them (matching the module's other input screens).
        component Field: Column {
            property alias label: fieldLabel.text
            property alias text: fieldInput.text
            property int index: 0
            property bool masked: false
            signal edited(string value)
            spacing: root.sh * 0.0166667 //8
            width: root.sw * 0.5 //320

            Text {
                id: fieldLabel
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0291667 //14
            }

            Rectangle {
                width: parent.width
                height: root.sh * 0.075 //36
                color: root.surfaceColor
                border.color: authRoot.focusIndex === index ? root.accentColor : root.tertiaryColor
                border.width: root.sh * 0.003125 //2

                TextInput {
                    id: fieldInput
                    anchors.fill: parent
                    anchors.margins: root.sh * 0.0166667 //8
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0375 //18
                    clip: true
                    echoMode: masked ? TextInput.Password : TextInput.Normal
                    focus: authRoot.focusIndex === index

                    onTextChanged: edited(text)

                    Keys.onPressed: function(event) {
                        event.accepted = false
                    }
                }
            }
        }

        // Group 1 — inputs (Server URL, Username, Password)
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.sh * 0.0333333 //16

            // Server URL field
            Field {
                label: "Server URL:"
                index: 0
                text: authRoot.serverUrl
                width: root.sw * 0.71875 //460
                anchors.horizontalCenter: parent.horizontalCenter
                onEdited: function(value) { authRoot.serverUrl = value }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: root.sw * 0.0375 //24

                // Username field — half of the Server URL width (minus the Row
                // spacing) so the two fields together line up with the field above.
                Field {
                    label: "Username:"
                    index: 1
                    text: authRoot.username
                    width: root.sw * 0.340625 //218
                    onEdited: function(value) { authRoot.username = value }
                }

                // Password field
                Field {
                    label: "Password:"
                    index: 2
                    masked: true
                    text: authRoot.password
                    width: root.sw * 0.340625 //218
                    onEdited: function(value) { authRoot.password = value }
                }
            }
        }

        // Group 2 — actions (status line + Sign In / Emby Connect buttons)
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.sh * 0.05 //24

            // status line: connecting > error > Emby Connect caption
            Text {
                text: waiting ? "CONNECTING..."
                    : (errorMsg !== "" ? errorMsg
                    : "SIGN IN TO A SERVER, OR USE EMBY CONNECT")
                color: errorMsg !== "" && !waiting ? root.accentColor : root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
                width: root.sw * 0.5
                elide: Text.ElideRight
                maximumLineCount: 1
                font.pixelSize: root.sh * 0.0291667 //14
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: root.sw * 0.025 //16

                // Sign In button
                Rectangle {
                    width: root.sw * 0.1875 //120
                    height: root.sh * 0.0583333 //28
                    color: focusIndex === 3 ? root.accentColor : root.surfaceColor
                    border.color: focusIndex === 3 ? root.accentColor : root.tertiaryColor
                    border.width: root.sh * 0.003125 //2
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        anchors.centerIn: parent
                        text: "Sign In"
                        color: focusIndex === 3 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0291667 //14
                    }
                }

                // OR separator
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "OR"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0291667 //14
                }

                // Emby Connect button (cloud account — server URL not required)
                Rectangle {
                    width: root.sw * 0.1875 //120
                    height: root.sh * 0.0583333 //28
                    color: focusIndex === 4 ? root.accentColor : root.surfaceColor
                    border.color: focusIndex === 4 ? root.accentColor : root.tertiaryColor
                    border.width: root.sh * 0.003125 //2
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        anchors.centerIn: parent
                        text: "Emby Connect"
                        color: focusIndex === 4 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0291667 //14
                    }
                }
            }
        }
    }

    // Footer
    Text {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SIGN IN"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
