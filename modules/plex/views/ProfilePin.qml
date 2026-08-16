import QtQuick
import Components

// Entry for a Plex Home profile's 4-digit PIN.
//
// Reached only when plex.tv refuses the user switch and asks for one — the
// profile's "protected" flag is not a reliable predictor, so this view is never
// shown speculatively. Note the name: "PinAuth" is the plex.tv/link code, this is
// the profile PIN, and they are unrelated.
//
// Built as a spinner rather than a text field because the app is driven by a
// remote and a gamepad, which have no digit keys. Digit keys still work when a
// keyboard is attached.
FocusScope {
    id: profilePinRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    // Success leaves via replaceWith, not navigateTo, so this view drops off the
    // nav stack once the PIN is accepted — backing out of the library should
    // return to whatever came before the prompt, never re-ask for the PIN.
    signal replaceWith(string path, var params)
    signal goBack()

    property string userId: navParams.userId || ""
    property string userTitle: navParams.title || ""
    property bool   reauth: navParams.reauth === true

    readonly property int slotCount: 4
    property var digits: [0, 0, 0, 0]
    property int cursor: 0

    property bool submitting: false
    property string errorMsg: navParams.wrongPin === true ? "INCORRECT PIN" : ""

    function setDigit(value) {
        var d = digits.slice()
        d[cursor] = (value + 10) % 10
        digits = d
    }

    function submit() {
        if (userId === "") return
        profilePinRoot.submitting = true
        profilePinRoot.errorMsg = ""
        var pin = digits.join("")
        if (reauth) plexBackend.reauth_select_user(userId, pin)
        else        plexBackend.select_user(userId, pin)
    }

    function cancel() {
        plexBackend.cancel_pending_pin()
        goBack()
    }

    Connections {
        target: plexBackend

        // Routing continues from here, mirroring UserSelect.qml — this view is on
        // screen when the retry lands, so it owns the handoff.
        function onServersLoaded(servers) {
            if (!profilePinRoot.reauth)
                profilePinRoot.replaceWith("ServerSelect.qml", { servers: servers })
        }

        function onAuthSuccess() {
            if (profilePinRoot.reauth)
                profilePinRoot.replaceWith("Libraries.qml", {})
        }

        // Rejected again — stay put and let them retry.
        function onUserPinRequired(userId, wrongPin) {
            profilePinRoot.submitting = false
        }

        function onErrorOccurred(msg) {
            profilePinRoot.submitting = false
            profilePinRoot.errorMsg = msg
        }
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
            || event.key === Qt.Key_Back) {
            cancel()
            event.accepted = true
            return
        }
        if (submitting) { event.accepted = true; return }

        if (event.key === Qt.Key_Up) {
            setDigit(digits[cursor] + 1)
        } else if (event.key === Qt.Key_Down) {
            setDigit(digits[cursor] - 1)
        } else if (event.key === Qt.Key_Left) {
            // Wraps, per the app-wide list convention. BACKSPACE is reserved for
            // leaving the view, so LEFT is how you go back to correct a digit.
            cursor = cursor > 0 ? cursor - 1 : slotCount - 1
        } else if (event.key === Qt.Key_Right) {
            cursor = cursor < slotCount - 1 ? cursor + 1 : 0
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            submit()
        } else if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            setDigit(event.key - Qt.Key_0)
            if (cursor < slotCount - 1) cursor++
        } else {
            return
        }
        event.accepted = true
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Enter PIN"
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
            text: profilePinRoot.userTitle !== ""
                  ? "Enter the PIN for " + profilePinRoot.userTitle
                  : "Enter your profile PIN"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.sw * 0.025 //16

            Repeater {
                model: profilePinRoot.slotCount

                Rectangle {
                    width: root.sw * 0.0625 //40
                    height: root.sh * 0.125 //60
                    color: "transparent"
                    border.width: root.sh * 0.0041667 //2
                    border.color: profilePinRoot.cursor === index
                                  ? root.accentColor : root.tertiaryColor

                    Text {
                        // The slot under the cursor shows its digit so the spinner
                        // is usable; the rest are masked.
                        text: profilePinRoot.cursor === index
                              ? profilePinRoot.digits[index] : "*"
                        color: profilePinRoot.cursor === index
                               ? root.accentColor : root.primaryColor
                        font.family: root.globalFont
                        anchors.centerIn: parent
                        font.pixelSize: root.sh * 0.0666667 //32
                    }
                }
            }
        }

        Text {
            visible: profilePinRoot.submitting
            text: "Checking..."
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }

        Text {
            visible: profilePinRoot.errorMsg !== "" && !profilePinRoot.submitting
            text: profilePinRoot.errorMsg
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            width: root.sw * 0.6
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0375 //18
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":DIGIT " + root.hints.change + ":CHANGE " + root.hints.select + ":SUBMIT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
