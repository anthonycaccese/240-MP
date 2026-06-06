import QtQuick

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string filePath:    navParams.filePath || ""
    property string itemTitle:   navParams.title    || ""

    property bool   overlayVisible:      false
    property int    savedPositionMs:     0
    property int    savedPlaylistPos:    -1
    property int    choiceIndex:         0
    property bool   loopOn:              false
    property string resumeSetting:       "ask"

    // Track last non-null values during playback for robust save on exit
    property int    lastKnownPositionMs:  0
    property int    lastKnownDurationMs:  0
    property int    lastKnownPlaylistPos: -1

    // Playlist resume correction: --start=T is global so every subsequent
    // video in the playlist would also start at T. We track which playlist
    // position was resumed from and seek to 0 on the first position event
    // after each advancement past that point.
    property int    resumedFromPlaylistPos: -1
    property bool   needsSeekToZero:        false

    focus: true

    Keys.onPressed: function(event) {
        if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                choiceIndex = 0
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                choiceIndex = 1
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                var startMs    = choiceIndex === 0 ? savedPositionMs  : 0
                var startPlPos = choiceIndex === 0 ? savedPlaylistPos : -1
                if (choiceIndex === 0 && startPlPos >= 0)
                    resumedFromPlaylistPos = startPlPos
                overlayVisible = false
                mpvController.loadAndPlay(filePath, startMs / 1000.0, 0, -1, [], loopOn, startPlPos)
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
            if (ms > 0) {
                if (needsSeekToZero) {
                    needsSeekToZero = false
                    mpvController.seekTo(0)
                    return
                }
                playerRoot.lastKnownPositionMs = ms
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }
        function onPlaylistPosChanged(pos) {
            if (pos >= 0) {
                if (resumedFromPlaylistPos >= 0 && pos > resumedFromPlaylistPos)
                    needsSeekToZero = true
                playerRoot.lastKnownPlaylistPos = pos
                playerRoot.lastKnownPositionMs  = 0
            }
        }

        function onPlaybackFinished(finalPositionMs, finalDurationMs) {
            var pos   = lastKnownPositionMs  || finalPositionMs
            var dur   = lastKnownDurationMs  || finalDurationMs
            var plPos = lastKnownPlaylistPos

            if (isPlaylist(filePath)) {
                // Always save playlist state — skip completion detection
                if (pos > 0 || plPos >= 0)
                    localFilesBackend.savePosition(filePath, pos, plPos)
            } else {
                // Single file: clear if near completion, save otherwise
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
        resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"

        if (resumeSetting === "no") {
            mpvController.loadAndPlay(filePath, 0.0, 0, -1, [], loopOn, -1)
            return
        }

        var saved    = localFilesBackend.getSavedPosition(filePath)
        var savedPos = saved.pos   || 0
        var savedPl  = (saved.plPos !== undefined && saved.plPos !== null) ? saved.plPos : -1

        if (resumeSetting === "yes") {
            if (savedPos > 0 && savedPl >= 0)
                resumedFromPlaylistPos = savedPl
            mpvController.loadAndPlay(filePath, savedPos > 0 ? savedPos / 1000.0 : 0.0,
                                      0, -1, [], loopOn, savedPos > 0 ? savedPl : -1)
        } else {
            if (savedPos > 0) {
                savedPositionMs  = savedPos
                savedPlaylistPos = savedPl
                overlayVisible   = true
            } else {
                mpvController.loadAndPlay(filePath, 0.0, 0, -1, [], loopOn, -1)
            }
        }
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
            height: root.sh * 0.2833333 //136

            Column {
                id: dialogColumn
                anchors.fill: parent
                spacing: root.sh * 0.05 //24

                Text {
                    text: "RESUME PLAYBACK?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: [
                            savedPlaylistPos >= 0
                                ? "Resume video " + (savedPlaylistPos + 1) + " at " + formatTime(savedPositionMs)
                                : "Resume from " + formatTime(savedPositionMs),
                            "Start from the beginning"
                        ]
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
                                text: modelData
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
                    text: "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:SELECT"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    function isPlaylist(path) {
        var lower = path.toLowerCase()
        return lower.endsWith(".m3u") || lower.endsWith(".m3u8")
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
