import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    // Focus rows: 0=video track, 1=audio track, 2=start button
    property int focusRow: 0

    property var videoFiles: []
    property var audioFiles: []
    property int videoIndex: 0
    property int audioIndex: 0   // 0 = "Video Audio"; 1+ = audioFiles[audioIndex - 1]

    property bool autoLaunch: false

    // Each row ends in a virtual SHUFFLE slot, offered only when there is actually
    // more than one thing to shuffle.
    property bool videoShuffleAvailable: videoFiles.length > 1
    property bool audioShuffleAvailable: audioFiles.length > 1

    // Video row:  0..N-1 = files,        N   = SHUFFLE
    // Audio row:  0 = VIDEO AUDIO, 1..N = files, N+1 = SHUFFLE
    property int videoSlotCount: videoFiles.length + (videoShuffleAvailable ? 1 : 0)
    property int audioSlotCount: audioFiles.length + 1 + (audioShuffleAvailable ? 1 : 0)
    property bool videoShuffleSelected: videoShuffleAvailable && videoIndex === videoFiles.length
    property bool audioShuffleSelected: audioShuffleAvailable && audioIndex === audioFiles.length + 1

    focus: true

    function launchPlayer(videoShuffle, audioShuffle) {
        navigateTo("Player.qml", {
            videoPath:  videoShuffle ? "" : videoFiles[videoIndex].path,
            videoPaths: videoShuffle ? videoFiles.map(function(f) { return f.path }) : [],
            audioPath:  (!audioShuffle && audioIndex > 0) ? audioFiles[audioIndex - 1].path : "",
            audioPaths: audioShuffle ? audioFiles.map(function(f) { return f.path }) : []
        }, { returnedFromPlayer: true })
    }

    Component.onCompleted: {
        videoFiles = ambientModeBackend.getVideoFiles()
        audioFiles = ambientModeBackend.getAudioFiles()
        autoLaunch = !!appCore.get_setting(moduleRoot.moduleId, "auto_launch")

        // Auto-launch only fires when the app boots straight into this module
        // (it's the configured startup module), not when someone navigates here
        // from the main menu, that screen still has to be reachable to change
        // video/audio/shuffle. Also skipped when we're only back here because
        // the player exited, or ESC would relaunch forever with no way out.
        if (autoLaunch && videoFiles.length > 0 && navParams.fromAppStartup && !navListState.returnedFromPlayer) {
            // Auto-launch always shuffles
            Qt.callLater(function() { launchPlayer(videoShuffleAvailable, audioShuffleAvailable) })
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            focusRow = Math.max(0, focusRow - 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            focusRow = Math.min(2, focusRow + 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            // Both rows wrap (app-wide list convention), which is what puts SHUFFLE
            // one Left press away from each row's default position.
            if (focusRow === 0 && videoSlotCount > 1)
                videoIndex = (videoIndex - 1 + videoSlotCount) % videoSlotCount
            else if (focusRow === 1 && audioSlotCount > 1)
                audioIndex = (audioIndex - 1 + audioSlotCount) % audioSlotCount
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            if (focusRow === 0 && videoSlotCount > 1)
                videoIndex = (videoIndex + 1) % videoSlotCount
            else if (focusRow === 1 && audioSlotCount > 1)
                audioIndex = (audioIndex + 1) % audioSlotCount
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (focusRow === 2 && videoFiles.length > 0)
                launchPlayer(videoShuffleSelected, audioShuffleSelected)
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    // Empty state
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333 //16
        visible: videoFiles.length === 0
        Text {
            text: "No items found"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.05 //24
        }
        Text {
            text: "Please add items in the ambient:mode media directory"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    // Body
    Item {
        visible: videoFiles.length > 0
        anchors.centerIn: parent
        width: root.sw * 0.76875 //492
        height: root.sh * 0.4083333 //196
        clip: true

        // Playback Settings
        Text {
            id: playbackSettingsLabel
            text: "Playback Settings:"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.top: parent.top
            anchors.topMargin: root.sh * 0.0145833 //7
            leftPadding: root.sw * 0.009375 //6
            rightPadding: root.sw * 0.009375 //6
            font.pixelSize: root.sh * 0.0291667 //14
        }

        // Video Track
        Item {
            id: videoTrackRow
            anchors.top: playbackSettingsLabel.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: root.sh * 0.0145833 //7
            height: root.sh * 0.0583333 //28

            Rectangle {
                anchors.fill: parent
                color: focusRow === 0 ? root.accentColor : "transparent"
            }

            Text {
                text: "Video"
                color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.009375 //6
                font.pixelSize: root.sh * 0.0416667 //20
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.009375 //6
                spacing: root.sw * 0.00625 //4

                Text {
                    text: "◄"
                    color: focusRow === 0 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                    visible: videoSlotCount > 1
                }
                // Capped and clipped so a long filename can't run under the row
                // label, with the same marquee scroll as views/ModuleSettings.qml.
                Item {
                    id: videoValueClip
                    width: Math.min(videoValueText.implicitWidth, root.sw * 0.35)
                    height: parent.height
                    clip: true

                    Text {
                        id: videoValueText
                        text: videoShuffleSelected ? "SHUFFLE" : videoFiles[videoIndex].name
                        color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        anchors.verticalCenter: parent.verticalCenter
                        x: 0
                        font.pixelSize: root.sh * 0.0416667 //20
                    }

                    SequentialAnimation {
                        running: focusRow === 0 && videoValueText.implicitWidth > videoValueClip.width
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) videoValueText.x = 0

                        PauseAnimation { duration: 1500 }
                        NumberAnimation {
                            target: videoValueText
                            property: "x"
                            to: videoValueClip.width - videoValueText.implicitWidth
                            duration: Math.abs(to) * 20
                        }
                        PauseAnimation { duration: 2000 }
                        PropertyAction { target: videoValueText; property: "x"; value: 0 }
                    }
                }
                Text {
                    text: "►"
                    color: focusRow === 0 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                    visible: videoSlotCount > 1
                }
            }
        }

        // Audio Track
        Item {
            id: audioTrackRow
            anchors.top: videoTrackRow.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: root.sh * 0.0583333 //28

            Rectangle {
                anchors.fill: parent
                color: focusRow === 1 ? root.accentColor : "transparent"
            }

            Text {
                text: "Audio"
                color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: root.sw * 0.009375 //6
                font.pixelSize: root.sh * 0.0416667 //20
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.009375 //6
                spacing: root.sw * 0.00625 //4

                Text {
                    text: "◄"
                    color: focusRow === 1 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                    visible: audioSlotCount > 1
                }
                Item {
                    id: audioValueClip
                    width: Math.min(audioValueText.implicitWidth, root.sw * 0.35)
                    height: parent.height
                    clip: true

                    Text {
                        id: audioValueText
                        text: audioShuffleSelected ? "SHUFFLE"
                            : audioIndex === 0 ? "VIDEO AUDIO"
                            : audioFiles[audioIndex - 1].name
                        color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        anchors.verticalCenter: parent.verticalCenter
                        x: 0
                        font.pixelSize: root.sh * 0.0416667 //20
                    }

                    SequentialAnimation {
                        running: focusRow === 1 && audioValueText.implicitWidth > audioValueClip.width
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) audioValueText.x = 0

                        PauseAnimation { duration: 1500 }
                        NumberAnimation {
                            target: audioValueText
                            property: "x"
                            to: audioValueClip.width - audioValueText.implicitWidth
                            duration: Math.abs(to) * 20
                        }
                        PauseAnimation { duration: 2000 }
                        PropertyAction { target: audioValueText; property: "x"; value: 0 }
                    }
                }
                Text {
                    text: "►"
                    color: focusRow === 1 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                    visible: audioSlotCount > 1
                }
            }
        }

        Row {
            id: startPlayback
            anchors.top: audioTrackRow.bottom
            anchors.topMargin: root.sh * 0.0583333 //28
            height: root.sh * 0.35 //56

            // PLAY button
            Rectangle {
                id: playButton
                color: focusRow === 2 ? root.accentColor : root.surfaceColor
                border.color: focusRow === 2 ? root.accentColor : root.tertiaryColor
                width: root.sw * 0.76875 //492
                height: root.sh * 0.1166667 //56
                border.width: root.sh * 0.003125 //2

                Text {
                    anchors.centerIn: parent
                    text: "START ►"
                    color: focusRow === 2 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.05 //24
                }
            }
        }
    }

    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.change + ":CHANGE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
