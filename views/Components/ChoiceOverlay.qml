import QtQuick

// A full-screen keyboard-driven chooser: a prompt, the thing being acted on, and
// a short list of options. Factored out of NfcCardWriter.qml's chooser so every
// prompt in the app reads the same way.
//
// The host binds `choices` to a list of { label, action } maps and acts on the
// action in onActivated — the action is what drives behavior, never the label
// text, which is free to change with state.
FocusScope {
    id: overlayRoot

    property string promptText:   ""
    property string subtitleText: ""
    property var    choices:      []

    property int choiceIndex: 0

    signal activated(string action)
    signal closed()

    visible: false
    focus: visible

    function open() {
        choiceIndex = 0
        visible = true
        forceActiveFocus()
    }

    function close() {
        visible = false
        closed()
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            close()
            event.accepted = true
            return
        }
        if (choices.length === 0) return

        if (event.key === Qt.Key_Up) {
            choiceIndex = (choiceIndex - 1 + choices.length) % choices.length
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            choiceIndex = (choiceIndex + 1) % choices.length
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            var choice = choices[choiceIndex]
            // Close first, so the host's onClosed focus restore lands before any
            // navigation the action triggers.
            close()
            if (choice) activated(choice.action)
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor

        Column {
            anchors.centerIn: parent
            width: root.sw * 0.76875
            spacing: root.sh * 0.05 //24

            Column {
                width: parent.width
                spacing: root.sh * 0.0166667 //8

                Text {
                    text: overlayRoot.promptText
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0333333 //16
                    width: parent.width
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    visible: overlayRoot.subtitleText !== ""
                    text: overlayRoot.subtitleText
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0416667 //20
                    width: parent.width
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Column {
                width: parent.width

                Repeater {
                    model: overlayRoot.choices
                    delegate: Item {
                        width: parent.width
                        height: root.sh * 0.0583333

                        Rectangle {
                            anchors.fill: choiceText
                            color: root.accentColor
                            visible: index === overlayRoot.choiceIndex
                        }

                        Text {
                            id: choiceText
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.label
                            color: index === overlayRoot.choiceIndex ? root.surfaceColor : root.primaryColor
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            topPadding: root.sh * 0.0041667
                            leftPadding: root.sw * 0.009375
                            rightPadding: root.sw * 0.009375
                            bottomPadding: root.sh * 0.00625
                            font.pixelSize: root.sh * 0.0416667
                        }
                    }
                }
            }

            Text {
                text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
                      + root.hints.select + ":SELECT"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333 //16
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
