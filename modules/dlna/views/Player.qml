import QtQuick

// Playback surface, modelled on the Local Files player: mpv renders behind the
// app window, this view just forwards keys to it and handles the exit.
FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string url:       navParams.url      || ""
    property string itemTitle: navParams.title    || ""
    property string objectId:  navParams.objectId || ""
    property int    startMs:   navParams.startMs  || 0

    // Chosen from the probed track list when available; 0 means "no opinion",
    // leaving mpv to match on language instead.
    property int    audioTrack: navParams.audioTrack || 0
    property int    subTrack:   navParams.subTrack   || 0

    // A sidecar subtitle advertised by the server. mpv cannot find these on its
    // own over HTTP — there is no directory to scan the way there is for a local
    // file — so the URL has to be handed to it explicitly.
    property string subtitleUri: navParams.subtitleUri || ""

    // mpv stops reporting once it exits, so the last live values are kept for
    // the resume write in onPlaybackEnded.
    property int lastKnownPositionMs: 0
    property int lastKnownDurationMs: 0

    focus: true

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
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
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) playerRoot.lastKnownPositionMs = ms
        }

        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }

        function onAudioCycleRequested() {
            mpvController.sendKey("#")
        }

        function onSubtitleCycleRequested() {
            mpvController.sendKey("j")
        }

        // Every mpv exit ("eof" / "stopped" / "failed") lands here. Handling it
        // is what returns control to the menu — without it the app sits on a
        // black screen when a video reaches its natural end.
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            var pos = playerRoot.lastKnownPositionMs || finalPositionMs
            var dur = playerRoot.lastKnownDurationMs || finalDurationMs

            if (playerRoot.objectId !== "") {
                // Watched to the end: drop the resume point so it offers a
                // fresh play next time. Otherwise remember where we stopped,
                // ignoring the first few seconds as an accidental open.
                if (dur > 0 && pos >= dur * 0.95)
                    dlnaBackend.saveResumePosition(playerRoot.objectId, 0)
                else if (pos > 5000)
                    dlnaBackend.saveResumePosition(playerRoot.objectId, pos)
            }
            playerRoot.goBack()
        }
    }

    Component.onCompleted: {
        if (url === "") {
            goBack()
            return
        }
        var audioLang = appCore.get_setting(moduleRoot.moduleId, "audio_lang") || "-"
        var subLang   = appCore.get_setting(moduleRoot.moduleId, "sub_lang")   || "-"

        // An explicitly chosen track wins; language matching is the fallback.
        var subLangs  = (subLang !== "-" && subTrack <= 0) ? [subLang] : []
        // MpvController exposes --slang but has no --alang parameter, so the
        // audio preference has to travel as an extra argument.
        var extraArgs = (audioLang !== "-" && audioTrack <= 0) ? ["--alang=" + audioLang] : []

        // Naming a subtitle language means wanting those subtitles shown; with
        // no preference, stay on forced-only.
        var subFiles = subtitleUri !== "" ? [subtitleUri] : []

        // 0 means "show subtitles": having a sidecar implies wanting it, and
        // forced-only would load the file but never display it.
        var subFlag = subTrack > 0 ? subTrack
                    : (subLang !== "-" || subtitleUri !== "") ? 0 : -1

        // Spelled out positionally because extraArgs is the 17th parameter.
        mpvController.loadAndPlay(
            url,                                   // url
            startMs > 0 ? startMs / 1000.0 : 0.0,  // startSeconds
            audioTrack,                            // audioTrack (0 = let mpv pick)
            subFlag,                               // subTrack
            subFiles,                              // subFiles -> --sub-file
            subLangs,                              // subLangs -> --slang
            false,                                 // loop
            -1,                                    // playlistStart
            0.0,                                   // transcodeOffsetSec
            "",                                    // plexToken
            false,                                 // muteAudio
            "",                                    // oscMode
            false,                                 // shuffle
            [],                                    // subTitles
            0.0,                                   // imageDurationSec
            false,                                 // imageContent
            extraArgs)                             // extraArgs -> --alang
    }
}
