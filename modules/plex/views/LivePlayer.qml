import QtQuick

// Live TV player. A slimmed sibling of Player.qml — live channels have no
// resume/duration/seek/autoplay-next semantics, so this drops all of that. It
// tunes the selected channel, hands the HLS stream to mpv, and keeps the tuner
// alive with a timeline ping. To watch a different channel the user exits back to
// the channel list and picks another (in-player channel switching is not wired up
// yet — pending a maintainer discussion on how to route keys past mpv).
FocusScope {
    id: livePlayerRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()

    // The channel to watch: { channelId, number, title }.
    property var    channel:   navParams.channel || ({})

    property string sessionId:     ""
    property string streamUrl:     ""
    property string plexToken:     ""
    property bool   playbackStarted: false
    property bool   exiting:        false

    focus: true

    function newSessionId() {
        var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        var id = ""
        for (var i = 0; i < 12; i++) id += chars[Math.floor(Math.random() * chars.length)]
        return id
    }

    // Tune the channel. tune_channel resolves asynchronously through
    // onStreamUrlReady, which loads the resulting HLS stream into mpv.
    function tune() {
        if (!channel.channelId) { goBack(); return }
        playbackStarted = false
        sessionId = newSessionId()
        plexBackend.tune_channel(channel.channelId, sessionId)
    }

    function teardown() {
        if (exiting) return
        exiting = true
        plexBackend.stop_live_session(sessionId)
        mpvController.stop()
    }

    // Forward keys to mpv (the same set Player.qml forwards) so its OSC works on
    // the Pi, where the Qt app owns the keyboard and relays via sendKey. On desktop
    // mpv has focus and handles these directly. Up/Down drive the OSC here, not
    // channel changes — to switch channels the user exits back to the channel list.
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            // Quit mpv; onPlaybackEnded drives the teardown + goBack.
            mpvController.sendKey("ESC")
            event.accepted = true
        } else if (event.key === Qt.Key_Backspace) {
            mpvController.sendKey("BS")
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            mpvController.sendKey("UP")
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            mpvController.sendKey("DOWN")
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            mpvController.sendKey("LEFT")
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            mpvController.sendKey("RIGHT")
            event.accepted = true
        } else if (event.key === Qt.Key_Space) {
            mpvController.sendKey("SPACE")
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            mpvController.sendKey("ENTER")
            event.accepted = true
        }
    }

    Connections {
        target: plexBackend

        function onStreamUrlReady(url, plexToken) {
            livePlayerRoot.streamUrl = url
            livePlayerRoot.plexToken = plexToken
            // Live always transcodes (HLS): no separate audio/sub tracks to pick.
            mpvController.loadAndPlay(url, 0, 0, -1, [], [], false, -1, 0.0, plexToken)
        }

        function onErrorOccurred(msg) {
            console.log("[LivePlayer] Backend error: " + msg)
            // A tune/stream failure leaves nothing playing — bail back to the list.
            if (!playbackStarted) { teardown(); goBack() }
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) livePlayerRoot.playbackStarted = true
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            // Any end (user quit, stream failure, or rare eof) tears down the tuner
            // and returns to the channel list.
            teardown()
            goBack()
        }
    }

    // Keep-alive: Plex reaps the DVR grab (and then 404s the stream) if the client
    // stops reporting the timeline. Ping from the moment we're tuned — not gated on
    // playbackStarted — since the grab is rolling before mpv reports its first frame.
    // update_live_timeline no-ops until a channel is tuned.
    Timer {
        interval: 8000
        repeat:   true
        running:  true
        onTriggered: plexBackend.update_live_timeline("playing")
    }

    Component.onCompleted: tune()

    // Black backdrop + loading text, shown until mpv's window takes over (mirrors
    // Player.qml).
    Rectangle {
        anchors.fill: parent
        color: "black"

        Text {
            text: "TUNING " + ((livePlayerRoot.channel.number
                                 ? livePlayerRoot.channel.number + "  " : "")
                               + (livePlayerRoot.channel.title || "")) + "..."
            color: "white"
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.centerIn: parent
            font.pixelSize: root.sh * 0.05 //24
            visible: !livePlayerRoot.playbackStarted
        }
    }
}
