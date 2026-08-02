import QtQuick
import Components

// Remote/keyboard button remapping, lets an extra physical key (e.g. a
// remote's "OK" button sending a code the app doesn't otherwise recognize)
// also fire one of the six navigation actions, on top of that action's
// default key. Backed by InputManager's keyRemap table (config.json
// app.remote_keymap.<action>), which is additive: it never removes an
// action's default key, so a bad remap can't lock the menus out from a
// plain keyboard. See src/input/InputManager.cpp for the runtime side.
FocusScope {
    id: remapRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})

    readonly property var actions: [
        { id: "up",     label: "Up" },
        { id: "down",   label: "Down" },
        { id: "left",   label: "Left" },
        { id: "right",  label: "Right" },
        { id: "select", label: "Select / OK" },
        { id: "back",   label: "Back" }
    ]

    property var rows: []
    property bool capturing: false
    property int captureIndex: -1

    // While capturing, InputManager suspends remap delivery and reports every
    // real input (keyboard key, Consumer Control button, mouse button) through
    // auxButtonPressed instead — without this, a key that's already bound
    // would be consumed by the remap filter and captured as its action's
    // default key rather than as itself, and capturing a bound mouse/remote
    // button would fire the action on the same press that bound it.
    onCapturingChanged: inputManager.setRemapCapture(capturing)
    Component.onDestruction: inputManager.setRemapCapture(false)

    function buildRows() {
        var list = []
        for (var i = 0; i < actions.length; i++) {
            var a = actions[i]
            var stored = appCore.get_setting("", "remote_keymap." + a.id)
            // A never-written key arrives as undefined or null depending on
            // the Qt version's invalid-QVariant conversion — check both.
            var hasCustom = stored !== undefined && stored !== null && stored !== "" && stored !== 0
            list.push({
                id: a.id,
                label: a.label,
                value: hasCustom ? ("default + " + inputManager.keyDisplayName(stored)) : "default"
            })
        }
        list.push({ id: "reset", label: "Reset to Defaults", isReset: true })
        rows = list
        if (rowList.currentIndex < 0 || rowList.currentIndex >= rows.length)
            rowList.currentIndex = 0
    }

    function startCapture(idx) {
        captureIndex = idx
        capturing = true
    }

    function finishCapture(qtKey) {
        if (captureIndex >= 0 && captureIndex < actions.length) {
            // One physical button, one action: InputManager's remap table is
            // keyed by the button, so a key left bound to two actions would
            // silently fire only one of them while both rows claim it. Clear
            // it from any other action before saving.
            for (var i = 0; i < actions.length; i++) {
                if (i !== captureIndex
                        && appCore.get_setting("", "remote_keymap." + actions[i].id) === qtKey)
                    appCore.save_setting("", "remote_keymap." + actions[i].id, 0)
            }
            appCore.save_setting("", "remote_keymap." + actions[captureIndex].id, qtKey)
        }
        capturing = false
        captureIndex = -1
        buildRows()
        rowList.forceActiveFocus()
    }

    function cancelCapture() {
        capturing = false
        captureIndex = -1
        rowList.forceActiveFocus()
    }

    function resetAll() {
        for (var i = 0; i < actions.length; i++)
            appCore.save_setting("", "remote_keymap." + actions[i].id, 0)
        buildRows()
    }

    Component.onCompleted: {
        buildRows()
        rowList.forceActiveFocus()
    }

    // The single capture feed: while capturing, InputManager reports every
    // real input — keyboard key, Consumer Control button, mouse button —
    // through this one signal (see setRemapCapture). Back-family keys cancel
    // instead of binding, so there is always a guaranteed way out.
    Connections {
        target: inputManager
        function onAuxButtonPressed(extendedKeyId) {
            if (!remapRoot.capturing)
                return
            if (extendedKeyId === Qt.Key_Escape || extendedKeyId === Qt.Key_Backspace
                    || extendedKeyId === Qt.Key_Back)
                remapRoot.cancelCapture()
            else
                remapRoot.finishCapture(extendedKeyId)
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        title: "Controls"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    ListView {
        id: rowList
        model: remapRoot.rows
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: !remapRoot.capturing

        Keys.onUpPressed: {
            if (currentIndex > 0) currentIndex-- 
            else {
                currentIndex = rows.length-1
            }
            rowList.positionViewAtIndex(currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            rowList.positionViewAtIndex(currentIndex, ListView.Contain)
        }
        Keys.onReturnPressed: {
            var row = remapRoot.rows[currentIndex]
            if (!row) return
            if (row.isReset) remapRoot.resetAll()
            else remapRoot.startCapture(currentIndex)
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                remapRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: rowList.width
            height: root.sh * 0.0583333 //28

            Rectangle {
                anchors.fill: parent
                color: rowList.currentIndex === index ? root.accentColor : "transparent"

                Text {
                    text: modelData.label || ""
                    color: rowList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
                }

                Text {
                    visible: !modelData.isReset
                    text: modelData.value || ""
                    color: rowList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: root.sw * 0.009375 //6
                    font.pixelSize: root.sh * 0.0375 //18
                }
            }
        }
    }

    // --- HELP TEXT ---
    Rectangle {
        visible: !remapRoot.capturing
        property color baseColor: root.primaryColor
        color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.2)
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1583333 //76
        anchors.leftMargin: root.sw * 0.125 //80
        width: root.sw * 0.75 //480
        height: root.sh * 0.0583333 //28
        clip: true
        Text {
            text: "Map one additional button for each action\n(the default button will continue to function)"
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0291667 //14
            wrapMode: Text.WordWrap
            anchors.fill: parent
            anchors.margins: root.sw * 0.0125 //6
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // --- FOOTER ---
    Text {
        visible: !remapRoot.capturing
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SET"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // --- CAPTURE OVERLAY ---
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: remapRoot.capturing
        focus: remapRoot.capturing

        // Real keys never reach here while capturing — InputManager swallows
        // them and reports through auxButtonPressed (see the Connections
        // above). What still arrives is the synthesized default-key events a
        // gamepad produces: let its Back button cancel, and swallow the rest
        // so a pad press can't bind its synthesized key or leak into the list
        // underneath. Autorepeat is ignored: the press that opened this
        // overlay may still be held when repeat kicks in.
        Keys.onPressed: function(event) {
            if (!event.isAutoRepeat
                    && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back))
                remapRoot.cancelCapture()
            event.accepted = true
        }

        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.025 //12

            Text {
                text: remapRoot.captureIndex >= 0
                      ? ("PRESS A NEW BUTTON FOR [" + remapRoot.actions[remapRoot.captureIndex].label.toUpperCase() + "]")
                      : ""
                color: root.accentColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.05 //24
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: root.hints.back + ":CANCEL"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333 //16
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
