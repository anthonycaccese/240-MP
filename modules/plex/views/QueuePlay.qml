import QtQuick

// Launcher for playing a whole set. Two modes, one loading frame:
//
//   queueItems   — PLAY ALL / SHUFFLE over a playlist or collection. Expands the
//                  set into ratingKeys and hands the Player the full ordered
//                  queue to advance through.
//   shuffleScope — SHUFFLE EPISODES from a show or season detail screen. Draws
//                  one random episode from the scope and hands the Player the
//                  scope itself, so it keeps drawing at every end of file. A
//                  jukebox: endless, and reports no timeline.
//
// Either way it resolves the first playable item, builds its stream, then
// replaces itself with Player.qml.
//
// replaceWith (not navigateTo) leaves no entry on the nav stack, so backing out
// of the player returns straight to the screen the set was started from — the
// list, or the show/season being shuffled — with its selected row restored. This
// mirrors CardPlay.qml, the other launcher that plays something without going
// through a detail screen.
FocusScope {
    id: queueRoot

    property var navParams: ({})

    signal replaceWith(string path, var params)
    signal goBack()

    // The rows the user queued, straight from the list view: leaves and shows.
    property var    queueItems: navParams.queueItems || []
    property bool   shuffle:    navParams.shuffle    || false
    property string queueTitle: navParams.title      || ""
    // Show or season ratingKey to draw random episodes from. Non-empty puts this
    // view in jukebox mode, where there is no queue at all — the Player redraws
    // from the scope itself.
    property string shuffleScope: navParams.shuffleScope || ""

    // The ratingKeys that actually play, once the shows have been expanded into
    // their episodes and the order has been settled.
    property var    queue:        []
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
        if (shuffleScope !== "") {
            launching = true
            // The backend's shuffle bag owns the ordering here, so there is
            // nothing to expand and nothing to shuffle client-side.
            plexBackend.load_random_episode(shuffleScope)
            return
        }
        if (queueItems.length === 0) { fail("NOTHING TO PLAY"); return }
        launching = true
        // Answers immediately when there is nothing to expand; a set containing
        // shows costs one request per show plus one per season first, which is
        // what the loading screen is covering.
        plexBackend.expand_queue(queueItems)
    }

    // Play the expanded queue from its first entry, in order or shuffled. The
    // shuffle happens here, after expansion, so it interleaves episodes across
    // shows rather than merely reordering the shows themselves.
    function startQueue(keys) {
        if (shuffle) {
            for (var i = keys.length - 1; i > 0; i--) {
                var j = Math.floor(Math.random() * (i + 1))
                var tmp = keys[i]; keys[i] = keys[j]; keys[j] = tmp
            }
        }
        queue = keys
        queueIndex = 0
        plexBackend.load_queue_item(queue[0])
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

        function onQueueReady(ratingKeys) {
            if (!queueRoot.launching) return
            if (ratingKeys.length === 0) { queueRoot.fail("NOTHING TO PLAY"); return }
            queueRoot.startQueue(ratingKeys)
        }

        function onNextEpisodeReady(detail) {
            if (!queueRoot.launching) return
            // An unplayable entry must not sink the whole queue — skip to the next
            // one, and only give up once nothing in the queue resolves.
            if (!detail || !detail.ratingKey) {
                // Jukebox mode has no queue to walk: an empty draw means the
                // show or season holds nothing playable.
                if (queueRoot.shuffleScope !== "") {
                    queueRoot.fail("NOTHING TO PLAY")
                    return
                }
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
                // not also fire at the end of an episode inside a playlist. A
                // jukebox is the other way round — it rides on that same chain,
                // so it leaves allowAutoplay alone and the user's
                // autoplay_next_episode setting decides whether it keeps rolling.
                allowAutoplay:      queueRoot.shuffleScope !== "",
                // A jukebox reports no timeline, so the show's watched state and
                // Continue Watching stay untouched; a queue is ordinary playback
                // and reports normally. The scope goes with it, so the Player
                // redraws from the show or season instead of walking a queue.
                trackProgress:      queueRoot.shuffleScope === "",
                shuffleScope:       queueRoot.shuffleScope,
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
