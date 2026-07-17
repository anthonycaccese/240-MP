import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string filePath:    navParams.filePath || ""
    property string itemTitle:   navParams.title    || ""

    property bool   overlayVisible:      false
    property int    choiceIndex:         0
    // Overlay choices, each { label, startMs, plPos, shuffle } — executed via play()
    property var    choices:             []
    property bool   loopOn:              false
    property string shuffleSetting:      "ask"
    property string resumeSetting:       "ask"
    property string subtitleMode:        "forced"
    property var    subtitleLangs:       []
    property int    imageDurationSec:    5

    // True when playback is images (a standalone image, or a playlist that contains
    // at least one image). Gates the slideshow-redraw mpv script — see MpvController.
    property bool   imageContent:        false

    // mpv subtitle-track flag derived from subtitleMode: 0 = on, -1 = forced only, -2 = off.
    property int    subFlag:             (subtitleMode == "on") ? 0 : ((subtitleMode == "forced") ? -1 : -2)

    // Track last non-null values during playback for robust save on exit
    property int    lastKnownPositionMs:  0
    property int    lastKnownDurationMs:  0
    property int    lastKnownPlaylistPos: -1

    focus: true

    Keys.onPressed: function(event) {
        if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                if (choiceIndex > 0) choiceIndex--
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                if (choiceIndex < choices.length - 1) choiceIndex++
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                var choice = choices[choiceIndex]
                overlayVisible = false
                play(choice.startMs, choice.plPos, choice.shuffle)
                event.accepted = true
            }
        } else {
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
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) playerRoot.lastKnownPositionMs = ms
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }
        function onPlaylistPosChanged(pos) {
            if (pos >= 0) {
                playerRoot.lastKnownPlaylistPos = pos
                playerRoot.lastKnownPositionMs  = 0
            }
        }

        // mpv exited for any reason ("eof"/"stopped"/"failed"). Local Files has no
        // autoplay-next or transcode-retry, so every exit is handled the same way:
        // save/clear the resume position and return to the menu. Handling the single
        // playbackEnded signal here is what keeps the app from freezing on a natural
        // end-of-file (the original bug was a missing per-reason handler).
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            var pos   = lastKnownPositionMs  || finalPositionMs
            var dur   = lastKnownDurationMs  || finalDurationMs
            var plPos = lastKnownPlaylistPos

            if (isPlaylist(filePath)) {
                // The per-item duration can't tell "finished the list" from
                // "finished one video of it", but the exit reason can: mpv only
                // ends with "eof" when the final item played to its end (a quit
                // mid-list leaves a trailing quit/stop end-file event). A
                // completed playlist clears its resume point like a completed
                // single video; anything else saves item + timecode.
                if (reason === "eof")
                    localFilesBackend.clearPosition(filePath)
                else if (pos > 0 || plPos >= 0)
                    localFilesBackend.savePosition(filePath, pos, plPos)
            } else if (!isImage(filePath)) {
                // Single file: clear if near completion, save otherwise.
                // Images carry no resume position, so they never write history.
                if (dur > 0 && pos >= dur * 0.95)
                    localFilesBackend.clearPosition(filePath)
                else if (pos > 5000)
                    localFilesBackend.savePosition(filePath, pos, -1)
            }
            goBack()
        }
    }

    Component.onCompleted: {
        if (filePath === "") return
        loopOn        = !!appCore.get_setting(moduleRoot.moduleId, "loop_playback")
        // Some fancy logic to honor the old boolean settings until they get updated to the new format
        var shufRaw   = appCore.get_setting(moduleRoot.moduleId, "shuffle_playback")
        shuffleSetting = (typeof shufRaw === "boolean") ? (shufRaw ? "yes" : "no") : (shufRaw || "ask")
        var autoSubs  = appCore.get_setting(moduleRoot.moduleId, "auto_subtitles")
        subtitleMode  = (typeof autoSubs === "boolean") ? ((autoSubs === true) ? "on" : "forced") : (autoSubs || "forced")
        var resRaw    = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        resumeSetting = (resRaw === "yes" || resRaw === "no") ? resRaw : "ask"
        var imgDur = parseFloat(appCore.get_setting(moduleRoot.moduleId, "image_duration"))
        imageDurationSec = isNaN(imgDur) ? 5 : imgDur

        imageContent = isImage(filePath) ||
                       (isPlaylist(filePath) && localFilesBackend.playlistContainsImages(filePath))

        // Leaving this as an array since MPV - like most players - expects a *list* of languages
        // to progressively fall back to until a sub track is found. If we ever switch back to
        // selecting a list in Settings, the change to support them all will be considerably simpler.
        // "-" is the value we store for "Any" (i.e. no preference) thats also the manifest default and
        // "Any" option's id. If the user never opened this setting, then get_setting returns nothing,
        // so it will fall back to "-" too. With this, "haven't picked one" will behave the same as "Any":
        // the check below adds nothing to the list and MPV is launched without a --slang preference.
        var subLangString = appCore.get_setting(moduleRoot.moduleId, "sub_lang") || "-"
        subtitleLangs = []
        if (subLangString !== "-") {
            subtitleLangs.push(subLangString)
        }

        // Shuffle only applies to playlists; "Always" wins over resume: a shuffled
        // playlist starts fresh & random; resume position (a sequential item index)
        // is meaningless once order is randomized.
        var canShuffle = isPlaylist(filePath)
        if (canShuffle && shuffleSetting === "yes") {
            play(0, -1, true)
            return
        }

        // A standalone image has no meaningful playback position, so it bypasses
        // resume entirely (no saved-position lookup, no "RESUME PLAYBACK?" overlay).
        // Images inside a playlist still resume via the playlist's item index below.
        if (!canShuffle && isImage(filePath)) {
            play(0, -1, false)
            return
        }

        var askShuffle = canShuffle && shuffleSetting === "ask"

        var savedPos = 0
        var savedPl  = -1
        if (resumeSetting !== "no") {
            var saved = localFilesBackend.getSavedPosition(filePath)
            savedPos  = saved.pos || 0
            savedPl   = (saved.plPos !== undefined && saved.plPos !== null) ? saved.plPos : -1
        }

        if (resumeSetting === "yes" && !askShuffle) {
            play(savedPos > 0 ? savedPos : 0, savedPos > 0 ? savedPl : -1, false)
            return
        }

        var opts = []
        if (savedPos > 0) {
            opts.push({ label: savedPl >= 0
                            ? "Resume video " + (savedPl + 1) + " at " + formatTime(savedPos)
                            : "Resume from " + formatTime(savedPos),
                        startMs: savedPos, plPos: savedPl, shuffle: false })
            if (resumeSetting === "ask")
                opts.push({ label: "Start from the beginning", startMs: 0, plPos: -1, shuffle: false })
        } else if (askShuffle) {
            opts.push({ label: "Play in order", startMs: 0, plPos: -1, shuffle: false })
        }
        if (askShuffle)
            opts.push({ label: "Shuffle", startMs: 0, plPos: -1, shuffle: true })

        if (opts.length > 1) {
            choices        = opts
            choiceIndex    = 0
            overlayVisible = true
        } else {
            play(0, -1, false)
        }
    }

    function play(startMs, plPos, shuffle) {
        mpvController.loadAndPlay(filePath, startMs > 0 ? startMs / 1000.0 : 0.0, 0, subFlag, [], subtitleLangs, loopOn, plPos, 0.0, "", false, "", shuffle, [], imageDurationSec, imageContent)
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: overlayVisible

        Rectangle {
            id: dialogRect
            color: root.surfaceColor
            anchors.centerIn: parent
            width: root.sw * 0.76875 //492
            height: root.sh * (0.2833333 + Math.max(0, choices.length - 2) * 0.0583333) //136 for 2 rows + 28 per extra row

            Column {
                id: dialogColumn
                anchors.fill: parent
                spacing: root.sh * 0.05 //24

                Text {
                    // Generic title whenever a Shuffle choice is offered; the classic
                    // resume-only dialog keeps its original wording.
                    text: choices.some(function(c) { return c.shuffle }) ? "START PLAYBACK?" : "RESUME PLAYBACK?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: choices
                        delegate: Item {
                            width: dialogColumn.width
                            height: root.sh * 0.0583333 //28

                            Rectangle {
                                anchors.fill: delegateText
                                color: root.accentColor
                                visible: index === choiceIndex
                            }

                            Text {
                                id: delegateText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: index === choiceIndex ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.capitalization: Font.AllUppercase
                                topPadding: root.sh * 0.0041667 //2
                                leftPadding: root.sw * 0.009375 //6
                                rightPadding: root.sw * 0.009375 //6
                                bottomPadding: root.sh * 0.00625 //3
                                font.pixelSize: root.sh * 0.0416667 //20
                            }
                        }
                    }
                }

                Text {
                    text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    function isPlaylist(path) {
        return localFilesBackend.isPlaylist(path)
    }

    function isImage(path) {
        return localFilesBackend.isImage(path)
    }

    function formatTime(ms) {
        var s   = Math.floor(ms / 1000)
        var h   = Math.floor(s / 3600)
        var m   = Math.floor((s % 3600) / 60)
        var sec = s % 60
        if (h > 0)
            return h + ":" + pad(m) + ":" + pad(sec)
        return m + ":" + pad(sec)
    }

    function pad(n) { return n < 10 ? "0" + n : "" + n }
}
