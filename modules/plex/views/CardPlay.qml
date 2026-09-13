import QtQuick

// NFC card deep-link target. Resolves the card's Plex ref, then replaces itself
// with the view that plays it: a single item's guid is resolved and streamed
// here, while a card naming a whole set (a collection or a playlist) hands off to
// QueuePlay.qml, which already owns expanding and ordering a queue.
//
// replaceWith (not navigateTo) is the point: this view leaves no entry on the
// module's nav stack, so backing out of the player unwinds straight to the NFC
// module's tap screen rather than stranding the user inside Plex, which they
// never chose to enter.
//
// The visual language here is the NFC module's, not Plex's — a card tap should
// look the same whatever module ends up serving it.
FocusScope {
    id: cardRoot

    property var navParams: ({})

    signal replaceWith(string path, var params)
    signal goBack()

    property string cardRef:   navParams.cardRef   || ""
    property string cardMode:  navParams.cardMode  || ""
    property string cardTitle: navParams.cardTitle || ""
    property string authState: navParams.authState || ""
    property string pendingPin: navParams.pendingPin || ""

    // A card that names a set rather than one item. Its ref carries the
    // collection's or playlist's ratingKey; the rows behind it are resolved at tap
    // time, so a card follows the set as its contents change.
    readonly property bool isQueueRef: cardRef.indexOf("plex://collection/") === 0
                                       || cardRef.indexOf("plex://playlist/") === 0

    property string errorMessage: ""
    property string sessionId:    ""
    // Guards against a stray streamUrlReady from an earlier view reaching us.
    property bool   launching:    false
    property var    pendingDetail: null

    focus: true

    function newSessionId() {
        var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        var id = ""
        for (var i = 0; i < 12; i++) id += chars[Math.floor(Math.random() * chars.length)]
        return id
    }

    function fail(msg) {
        launching = false
        errorMessage = msg
    }

    function start() {
        errorMessage = ""
        // Cards never switch users or servers, so an unusable session is simply an
        // error the user resolves in the Plex module itself.
        if (authState === "none") { fail("NOT SIGNED IN TO PLEX"); return }
        if (authState === "needs_user") { fail("NO PLEX SERVER SELECTED"); return }
        if (pendingPin !== "") { fail("THIS PLEX PROFILE NEEDS ITS PIN\n\nOPEN THE PLEX MODULE TO SIGN IN"); return }
        if (cardRef === "") { fail("THIS CARD HAS NO PLEX ITEM"); return }
        launching = true
        if (isQueueRef) {
            plexBackend.resolve_card_queue(cardRef)
            return
        }
        plexBackend.resolve_card(cardRef, cardMode)
    }

    Keys.onPressed: function(event) {
        if (errorMessage === "") return
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            start()
            event.accepted = true
        }
    }

    Connections {
        target: plexBackend

        function onCardItemReady(detail) {
            if (!cardRoot.launching) return
            cardRoot.pendingDetail = detail
            cardRoot.sessionId = cardRoot.newSessionId()
            // Unlike Item.qml, no set_audio_stream / set_subtitle_stream call: a
            // card tap must not rewrite the server's stored per-part track
            // defaults. Whatever the server already prefers is what plays.
            if (detail.forceTranscode) {
                // Always from 0 so the whole timeline stays seekable; Player.qml
                // seeks to the resume point itself.
                plexBackend.request_transcode(detail.ratingKey, detail.partKey, cardRoot.sessionId,
                                              detail.selectedAudioId || "",
                                              detail.selectedSubtitleId || "0", 0)
            } else {
                plexBackend.build_stream_url(detail.ratingKey, detail.partKey, cardRoot.sessionId)
            }
        }

        // A set card never streams anything here: QueuePlay expands the rows,
        // settles their order and launches the Player, exactly as the in-app
        // PLAY ALL / SHUFFLE rows do. replaceWith again, so the stack still
        // unwinds straight back to the NFC tap screen.
        //
        // "shuffle" means shuffle this queue — not the endless jukebox a show or
        // season card gets. A set is finite and reports its timeline normally.
        function onCardQueueReady(items) {
            if (!cardRoot.launching) return
            cardRoot.launching = false
            cardRoot.replaceWith("QueuePlay.qml", {
                queueItems: items,
                shuffle:    cardRoot.cardMode === "shuffle",
                title:      cardRoot.cardTitle
            })
        }

        function onCardError(message) {
            if (!cardRoot.launching) return
            cardRoot.fail(message)
        }

        function onStreamUrlReady(url, plexToken) {
            if (!cardRoot.launching || !cardRoot.pendingDetail) return
            var d = cardRoot.pendingDetail
            cardRoot.launching = false
            cardRoot.replaceWith("Player.qml", {
                streamUrl:          url,
                plexToken:          plexToken,
                ratingKey:          d.ratingKey,
                partKey:            d.partKey,
                partId:             d.partId,
                sessionId:          cardRoot.sessionId,
                viewOffset:         d.viewOffset || 0,
                title:              d.title,
                audioStreams:       d.audioStreams,
                subtitleStreams:    d.subtitleStreams,
                isTranscoding:      d.forceTranscode || false,
                selectedAudioId:    d.selectedAudioId,
                selectedSubtitleId: d.selectedSubtitleId,
                // A shuffle card is a jukebox: report no timeline, so it leaves the
                // show's watched state and Continue Watching untouched.
                trackProgress:      d.trackProgress !== false,
                // Set only for a shuffle card. It makes the Player draw the next
                // episode from the card's show/season instead of taking the
                // sequential next one — a shuffle card keeps rolling, it just
                // rolls randomly. Autoplay itself is left to the user's setting.
                shuffleScope:       d.cardScope || ""
            })
        }

        function onErrorOccurred(message) {
            if (!cardRoot.launching) return
            cardRoot.fail(message)
        }
    }

    Component.onCompleted: {
        // A card tap is user activity but arrives with no key event — without this
        // the loading frame and any error would render behind the screen saver.
        root.dismissScreenSaver()
        start()
    }

    Rectangle {
        anchors.fill: parent
        color: "black"

        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.05
            visible: errorMessage === ""

            Text {
                text: "LOADING..."
                color: "white"
                font.family: root.globalFont
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: root.sh * 0.05
            }
            Text {
                visible: cardTitle !== ""
                text: cardTitle
                color: "#919191"
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                width: root.sw * 0.76875
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: root.sh * 0.0333333
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.05
            visible: errorMessage !== ""

            Text {
                text: errorMessage
                color: "white"
                font.family: root.globalFont
                width: root.sw * 0.5625
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: root.sh * 0.0375
            }
            Text {
                text: root.hints.back + ":BACK " + root.hints.select + ":RETRY"
                color: "#919191"
                font.family: root.globalFont
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: root.sh * 0.0333333
            }
        }
    }
}
