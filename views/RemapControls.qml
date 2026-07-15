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

    function buildRows() {
        var list = []
        for (var i = 0; i < actions.length; i++) {
            var a = actions[i]
            var stored = appCore.get_setting("", "remote_keymap." + a.id)
            var hasCustom = stored !== undefined && stored !== "" && stored !== 0
            list.push({
                id: a.id,
                label: a.label,
                value: hasCustom ? ("+ " + inputManager.keyDisplayName(stored)) : "(default only)"
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
        if (captureIndex >= 0 && captureIndex < actions.length)
            appCore.save_setting("", "remote_keymap." + actions[captureIndex].id, qtKey)
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

    // Buttons on the auxiliary "Consumer Control" input device (Home, Back,
    // Menu, colored buttons, zoom, etc. on some remotes) never generate a Qt
    // key event at all, so the capture overlay's Keys.onPressed can't see
    // them, this is the other half of that capture, fed by InputManager
    // reading the device directly. See InputManager::openConsumerControlDevice.
    Connections {
        target: inputManager
        function onAuxButtonPressed(extendedKeyId) {
            if (remapRoot.capturing)
                remapRoot.finishCapture(extendedKeyId)
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Remote Controls"
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

        Keys.onUpPressed:   currentIndex = Math.max(0, currentIndex - 1)
        Keys.onDownPressed: currentIndex = Math.min(rows.length - 1, currentIndex + 1)
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
            text: "Adds an extra button for that action, the default key keeps working"
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

        // Any key other than Back/Escape becomes the new binding; Back always
        // cancels rather than being bindable, so there's always a guaranteed
        // way out of this screen. Autorepeat is ignored: the same physical
        // press that opened this overlay is often still held when OS repeat
        // kicks in, and without this check that repeat immediately "finishes"
        // capture with the very key that started it.
        Keys.onPressed: function(event) {
            if (event.isAutoRepeat) {
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                remapRoot.cancelCapture()
            } else {
                remapRoot.finishCapture(event.key)
            }
            event.accepted = true
        }

        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.025 //12

            Text {
                text: remapRoot.captureIndex >= 0
                      ? ("PRESS A BUTTON FOR " + remapRoot.actions[remapRoot.captureIndex].label.toUpperCase())
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
