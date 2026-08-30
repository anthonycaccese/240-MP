import QtQuick

// PLAY ALL / SHUFFLE launcher for a playlist or collection. Resolves the first
// playable item of the queue, builds its stream, then replaces itself with
// Player.qml — which carries the rest of the queue and advances through it.
//
// replaceWith (not navigateTo) leaves no entry on the nav stack, so backing out
// of the player returns straight to the list the queue was started from, with
// its selected row restored. This mirrors CardPlay.qml, the other launcher that
// plays something without going through a detail screen.
FocusScope {
    id: queueRoot

    property var navParams: ({})

    signal replaceWith(string path, var params)
    signal goBack()

    // Ordered ratingKeys — already shuffled by the caller for SHUFFLE.
    property var    queue:     navParams.queue || []
    property string queueTitle: navParams.title || ""

    property int    queueIndex:   0
    property string errorMessage: ""
    property string sessionId:    ""
    // Guards against a stray nextEpisodeReady / streamUrlReady from an earlier
    // view reaching us.
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
        if (queue.length === 0) { fail("NOTHING TO PLAY"); return }
        // A retry after "nothing resolved" starts the queue over from the top.
        if (queueIndex >= queue.length) queueIndex = 0
        launching = true
        plexBackend.load_queue_item(queue[queueIndex])
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

        function onNextEpisodeReady(detail) {
            if (!queueRoot.launching) return
            // An unplayable entry must not sink the whole queue — skip to the next
            // one, and only give up once nothing in the queue resolves.
            if (!detail || !detail.ratingKey) {
                queueRoot.queueIndex++
                if (queueRoot.queueIndex >= queueRoot.queue.length) {
                    queueRoot.fail("COULD NOT LOAD THIS ITEM")
                    return
                }
                plexBackend.load_queue_item(queueRoot.queue[queueRoot.queueIndex])
                return
            }
            queueRoot.pendingDetail = detail
            queueRoot.sessionId = queueRoot.newSessionId()
            // Like CardPlay.qml, no set_audio_stream / set_subtitle_stream call:
            // the first item plays the server's stored defaults, and Player.qml
            // carries that selection forward by language from there.
            if (detail.forceTranscode) {
                // Always from 0 — a queue launch starts the item at its beginning.
                plexBackend.request_transcode(detail.ratingKey, detail.partKey, queueRoot.sessionId,
                                              detail.selectedAudioId || "",
                                              detail.selectedSubtitleId || "0", 0)
            } else {
                plexBackend.build_stream_url(detail.ratingKey, detail.partKey, queueRoot.sessionId)
            }
        }

        function onStreamUrlReady(url, plexToken) {
            if (!queueRoot.launching || !queueRoot.pendingDetail) return
            var d = queueRoot.pendingDetail
            queueRoot.launching = false
            queueRoot.replaceWith("Player.qml", {
                streamUrl:          url,
                plexToken:          plexToken,
                ratingKey:          d.ratingKey,
                partKey:            d.partKey,
                partId:             d.partId,
                sessionId:          queueRoot.sessionId,
                // A queue always starts its items from the beginning, so no resume
                // prompt on the first one either — every later item starts at 0 too.
                viewOffset:         0,
                title:              d.title,
                audioStreams:       d.audioStreams,
                subtitleStreams:    d.subtitleStreams,
                isTranscoding:      d.forceTranscode || false,
                selectedAudioId:    d.selectedAudioId,
                selectedSubtitleId: d.selectedSubtitleId,
                // The queue owns advancing: the season-based autoplay chain must
                // not also fire at the end of an episode inside a playlist.
                allowAutoplay:      false,
                queue:              queueRoot.queue,
                queueIndex:         queueRoot.queueIndex
            })
        }

        function onErrorOccurred(message) {
            if (!queueRoot.launching) return
            queueRoot.fail(message)
        }
    }

    Component.onCompleted: start()

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
                visible: queueTitle !== ""
                text: queueTitle
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
