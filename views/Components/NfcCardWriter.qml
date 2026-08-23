import QtQuick

// Full-screen takeover that writes an NFC card for the item a detail view is
// showing. Shared so every module that can be reached from a card writes them
// the same way; the host view supplies only what goes on the card.
//
// Flow: armed ("TAP A CARD") → captured → choose mode (or confirm a replace) →
// written. Capture is armed only while this is open, so a card resting near the
// reader while browsing can never trigger a write.
//
// The host is responsible for showing this (open()) and for whether the entry
// point is offered at all — see available.
FocusScope {
    id: writerRoot

    // What gets written.
    property string cardRef:   ""     // line 2, e.g. "plex://movie/…"
    property string cardTitle: ""     // filename + display title
    // Shows the shuffle option. Only meaningful for a show or season: shuffling
    // a movie has no meaning, and the card format ignores mode for one anyway.
    property bool   offerShuffle: false

    // True when a card could actually be written right now. Hosts bind their
    // entry-point row's visibility to this.
    readonly property bool available:
        typeof nfcReaderBackend !== "undefined"
        && nfcReaderBackend.available
        && nfcReaderBackend.readerConnected
        && appCore.is_module_enabled("com.240mp.nfc_reader")

    // "armed" | "choose" | "replace" | "done" | "failed"
    property string phase: "armed"
    property string capturedUid: ""
    property string existingTitle: ""
    property int    choiceIndex: 0
    property bool   pendingShuffle: false

    signal closed()

    visible: false
    focus: visible

    function open() {
        phase = "armed"
        capturedUid = ""
        existingTitle = ""
        choiceIndex = 0
        pendingShuffle = false
        visible = true
        nfcReaderBackend.setCardCapture(true)
        forceActiveFocus()
    }

    function close() {
        nfcReaderBackend.setCardCapture(false)
        visible = false
        closed()
    }

    // Each choice carries a stable action alongside its label. The action is what
    // drives behaviour — never the label text, which is free to be reworded.
    function choices() {
        if (phase === "replace")
            return [{ label: "Change this card", action: "write" },
                    { label: "Cancel",           action: "cancel" }]
        if (offerShuffle)
            return [{ label: "Write card (Sequential Episodes)", action: "write" },
                    { label: "Write card (Shuffle Episodes)",    action: "shuffle" },
                    { label: "Cancel",                           action: "cancel" }]
        return [{ label: "Write card", action: "write" },
                { label: "Cancel",     action: "cancel" }]
    }

    function commit() {
        var ok = nfcReaderBackend.writeCardFile(capturedUid, cardTitle, cardRef,
                                                pendingShuffle ? "shuffle" : "")
        phase = ok ? "done" : "failed"
    }

    function activate() {
        var choice = choices()[choiceIndex]
        if (!choice || choice.action === "cancel") { close(); return }

        // The replace step re-confirms an already-chosen mode; don't re-read it
        // from the (two-option) replace list, which has no shuffle entry.
        if (phase === "replace") { commit(); return }

        pendingShuffle = (choice.action === "shuffle")
        // Confirm before overwriting a card that already plays something —
        // a card is a physical object the user may not remember assigning.
        if (existingTitle !== "") {
            phase = "replace"
            choiceIndex = 0
            return
        }
        commit()
    }

    Connections {
        target: writerRoot.visible && typeof nfcReaderBackend !== "undefined" ? nfcReaderBackend : null
        function onCardCaptured(uid, existing) {
            if (writerRoot.phase !== "armed") return
            // One card per arm: disarm so a card left on the reader can't walk
            // the dialog forward underneath the user.
            nfcReaderBackend.setCardCapture(false)
            writerRoot.capturedUid   = uid
            writerRoot.existingTitle = existing
            writerRoot.choiceIndex   = 0
            writerRoot.phase         = "choose"
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            close()
            event.accepted = true
            return
        }
        if (phase === "done" || phase === "failed") {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                close()
                event.accepted = true
            }
            return
        }
        if (phase === "armed") return   // nothing to choose until a card arrives

        var list = choices()
        if (event.key === Qt.Key_Up) {
            choiceIndex = (choiceIndex - 1 + list.length) % list.length
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            choiceIndex = (choiceIndex + 1) % list.length
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            activate()
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
                    text: writerRoot.phase === "armed"    ? "TAP A CARD FOR..."
                        : writerRoot.phase === "replace"  ? "THIS CARD PLAYS \"" + writerRoot.existingTitle + "\". CHANGE TO..."
                        : writerRoot.phase === "done"     ? "CARD WRITTEN FOR..."
                        : writerRoot.phase === "failed"   ? "COULD NOT WRITE CARD"
                        :                                   "WRITE CARD FOR..."
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0333333 //16
                    width: parent.width
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    text: writerRoot.cardTitle
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
                visible: writerRoot.phase === "choose" || writerRoot.phase === "replace"

                Repeater {
                    model: writerRoot.choices()
                    delegate: Item {
                        width: parent.width
                        height: root.sh * 0.0583333

                        Rectangle {
                            anchors.fill: choiceText
                            color: root.accentColor
                            visible: index === writerRoot.choiceIndex
                        }

                        Text {
                            id: choiceText
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.label
                            color: index === writerRoot.choiceIndex ? root.surfaceColor : root.primaryColor
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
                text: writerRoot.phase === "armed"
                        ? root.hints.back + ":CANCEL"
                        : (writerRoot.phase === "done" || writerRoot.phase === "failed")
                          ? root.hints.select + ":DONE"
                          : root.hints.back + ":CANCEL " + root.hints.navigate + ":NAVIGATE "
                            + root.hints.select + ":SELECT"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
