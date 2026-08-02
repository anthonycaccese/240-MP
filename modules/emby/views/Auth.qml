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

    // The sign-in method is chosen FIRST, because it decides what the form
    // below asks for: a direct sign-in needs a server URL and the server
    // account username, while Emby Connect needs the emby.media email and no
    // server URL at all (connect_authenticate ignores it — the server is
    // resolved from the account). Asking for credentials before the method is
    // known meant the field could only be labelled for one of the two.
    property int method: 0   // 0 = Direct to Server, 1 = Emby Connect

    // Focus order, which shifts because Connect drops the server URL row:
    //   Direct : 0 method, 1 server URL, 2 username, 3 password, 4 Sign In
    //   Connect: 0 method,              1 email,     2 password, 3 Sign In
    property int focusIndex: 0
    readonly property int idxMethod: 0
    // -1 when Emby Connect is active: the server URL field is hidden but still
    // exists, and if its index matched the email field's the hidden TextInput
    // would win the focus binding and swallow every keystroke.
    readonly property int idxServer: method === 0 ? 1 : -1
    readonly property int idxUser:   method === 0 ? 2 : 1
    readonly property int idxPass:   method === 0 ? 3 : 2
    readonly property int idxSubmit: method === 0 ? 4 : 3
    readonly property int itemCount: method === 0 ? 5 : 4

    // Switching method can strand focus past the end of the shorter Connect
    // sequence; a stale error from the other method is also no longer relevant.
    function setMethod(m) {
        if (method === m) return
        method = m
        errorMsg = ""
        if (focusIndex > idxSubmit) focusIndex = idxSubmit
    }

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
        // On the method row Left/Right pick between the two options (radio
        // semantics — the pair sits side by side, so horizontal keys selecting
        // between them is what the layout implies). Everywhere else they step
        // like Tab, as on the rest of the form.
        if (focusIndex === idxMethod
            && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
            authRoot.setMethod(event.key === Qt.Key_Right ? 1 : 0)
            event.accepted = true
            return
        }
        // Every field/button is one linear sequence. Down/Right/Tab advance
        // through it and Up/Left/Shift+Tab retreat, wrapping around at the ends
        // — so all of them behave like Tab. (This means Left/Right no longer
        // move the text cursor within a field.)
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Right
            || event.key === Qt.Key_Tab) {
            focusIndex = (focusIndex + 1) % itemCount
            event.accepted = true
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left
                   || event.key === Qt.Key_Backtab) { // Backtab = Shift+Tab
            focusIndex = (focusIndex + itemCount - 1) % itemCount
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // Enter on the method row confirms the choice and moves into the
            // form rather than submitting an empty one.
            if (focusIndex === idxMethod) focusIndex = idxMethod + 1
            else if (method === 1) authRoot.submitConnect()
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

    // Emby Connect: cloud sign-in with the emby.media account (email +
    // password). No server URL is involved — servers are discovered from the
    // account, and a picker appears if there is more than one.
    function submitConnect() {
        if (waiting) return
        if (username === "") {
            errorMsg = "Enter your Emby Connect email"
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
        // Tightened from the pre-method-row values (134 / 36): the extra
        // method row made the form tall enough that the Sign In button ran
        // into the footer hints.
        anchors.topMargin: root.sh * 0.25 //120
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.sh * 0.05 //24

        // Reusable labelled text field. Up/Down navigation and Enter/Escape are
        // handled by authRoot.Keys — each TextInput forwards keys up by not
        // accepting them (matching the module's other input screens).
        component Field: Column {
            id: fieldRoot
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
                    // A hidden field must never hold focus — it would take the
                    // keystrokes meant for whichever field replaced it.
                    focus: fieldRoot.visible && authRoot.focusIndex === index

                    onTextChanged: edited(text)

                    Keys.onPressed: function(event) {
                        event.accepted = false
                    }
                }
            }
        }

        // One of the two sign-in methods. Selecting is what focusing does — the
        // row is a single focus stop and Left/Right move between the options,
        // so there is no separate "confirm" step to forget.
        component MethodOption: Rectangle {
            id: opt
            property string label: ""
            property int value: 0
            readonly property bool selected: authRoot.method === value
            readonly property bool rowFocused: authRoot.focusIndex === authRoot.idxMethod

            width: root.sw * 0.321875 //218
            height: root.sh * 0.0583333 //28
            color: selected && rowFocused ? root.accentColor : root.surfaceColor
            border.color: selected ? root.accentColor : root.tertiaryColor
            border.width: root.sh * 0.003125 //2

            Text {
                anchors.centerIn: parent
                text: opt.label
                color: opt.selected && opt.rowFocused ? root.surfaceColor
                     : (opt.selected ? root.accentColor : root.secondaryColor)
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0291667 //14
            }
        }

        // Group 1 — inputs (method, then the credentials that method needs)
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.sh * 0.05 //24

            // Sign-in method — first, because it decides everything below it
            Column {
                anchors.horizontalCenter: parent.horizontalCenter

                Row {
                    spacing: root.sw * 0.025 //16
                    MethodOption { label: "Local Server"; value: 0 }
                    Text {
                        text: "OR"
                        color: root.tertiaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0291667 //14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MethodOption { label: "Emby Connect";   value: 1 }
                }
            }
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: root.sh * 0.033 //16

                // Server URL — direct sign-in only; Emby Connect resolves the
                // server from the account, so showing it there would imply it
                // matters. Column positioners skip invisible children, so the row
                // collapses rather than leaving a gap.
                Field {
                    visible: authRoot.method === 0
                    label: "Server URL:"
                    index: authRoot.idxServer
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
                        // Direct sign-in takes the server account username; Emby
                        // Connect takes the emby.media email. The method is already
                        // chosen by the time this is reached, so the label is
                        // always the one that will actually work.
                        label: authRoot.method === 1 ? "Email:" : "Username:"
                        index: authRoot.idxUser
                        text: authRoot.username
                        width: root.sw * 0.340625 //218
                        onEdited: function(value) { authRoot.username = value }
                    }

                    // Password field
                    Field {
                        label: "Password:"
                        index: authRoot.idxPass
                        masked: true
                        text: authRoot.password
                        width: root.sw * 0.340625 //218
                        onEdited: function(value) { authRoot.password = value }
                    }
                }
            }
        }

        // Group 2 — actions (status line + Sign In / Emby Connect buttons)
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.sh * 0.025 //16

            // status line: connecting > error > guidance for the current step
            Text {
                text: waiting ? "CONNECTING..."
                    : (errorMsg !== "" ? errorMsg
                    : (focusIndex === idxMethod ? "CHOOSE HOW TO SIGN IN"
                    : (method === 1 ? "EMBY CONNECT USES YOUR EMBY.MEDIA EMAIL"
                    : "SIGN IN WITH YOUR SERVER ACCOUNT")))
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

            // Single submit — the method row above already decided which flow
            // this runs, so two competing action buttons are no longer needed.
            Rectangle {
                width: root.sw * 0.1875 //120
                height: root.sh * 0.0583333 //28
                anchors.horizontalCenter: parent.horizontalCenter
                color: focusIndex === idxSubmit ? root.accentColor : root.surfaceColor
                border.color: focusIndex === idxSubmit ? root.accentColor : root.tertiaryColor
                border.width: root.sh * 0.003125 //2

                Text {
                    anchors.centerIn: parent
                    text: "Sign In"
                    color: focusIndex === idxSubmit ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0291667 //14
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
