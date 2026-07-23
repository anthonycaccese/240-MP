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
        if (event.key === Qt.Key_Up) {
            if (focusIndex > 0) focusIndex--
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            if (focusIndex < 4) focusIndex++
            event.accepted = true
        } else if (event.key === Qt.Key_Tab) {
            focusIndex = (focusIndex + 1) % 5
            event.accepted = true
        } else if (event.key === Qt.Key_Backtab) { // Shift+Tab
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

    // Body — anchored below the AppBar (not centered): this form is tall enough
    // that vertical centering pushes its top row up into the header.
    Column {
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.195
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.sh * 0.02

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
            anchors.horizontalCenter: parent.horizontalCenter

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
                    // Inputs render exactly as typed — an uppercase-forced display
                    // reads as "my input was mangled" when entering emails/usernames.
                    font.capitalization: Font.MixedCase
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

        // Server URL field
        Field {
            label: "Server URL"
            index: 0
            text: authRoot.serverUrl
            onEdited: function(value) { authRoot.serverUrl = value }
        }

        // Username field
        Field {
            label: "Username"
            index: 1
            text: authRoot.username
            onEdited: function(value) { authRoot.username = value }
        }

        // Password field
        Field {
            label: "Password"
            index: 2
            masked: true
            text: authRoot.password
            onEdited: function(value) { authRoot.password = value }
        }

        // Sign In button
        Rectangle {
            width: root.sw * 0.234375 //150
            height: root.sh * 0.0583333 //28
            color: focusIndex === 3 ? root.accentColor : root.surfaceColor
            border.color: focusIndex === 3 ? root.accentColor : root.tertiaryColor
            border.width: root.sh * 0.003125 //2
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                anchors.centerIn: parent
                text: "Sign In"
                color: focusIndex === 3 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0375 //18
            }
        }

        // Emby Connect button (cloud account — server URL not required)
        Rectangle {
            width: root.sw * 0.234375 //150
            height: root.sh * 0.0583333 //28
            color: focusIndex === 4 ? root.accentColor : root.surfaceColor
            border.color: focusIndex === 4 ? root.accentColor : root.tertiaryColor
            border.width: root.sh * 0.003125 //2
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                anchors.centerIn: parent
                text: "Emby Connect"
                color: focusIndex === 4 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0375 //18
            }
        }

        // Single status line: connecting > error > Emby Connect caption. Merged
        // into one slot so the column keeps a fixed height and never runs into
        // the footer hints.
        Text {
            text: waiting ? "CONNECTING..."
                : (errorMsg !== "" ? errorMsg
                : "Emby Connect uses your emby.media account")
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
