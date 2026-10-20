import QtQuick
import Components

// Playlist list from youtube_playlists.txt, in file order (the user's own
// curation is the sort — no letter-nav panel, unlike Channels).
FocusScope {
    id: playlistsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    // Play/Shuffle prompt, shown when "Shuffle Playlists" is [ASK]
    property bool shuffleOverlayVisible: false
    property int  shuffleChoiceIndex: 0   // 0 = play in order, 1 = shuffle

    // Opens the video list, the same as before shuffle existed.
    function openInOrder(pl) {
        playlistsRoot.navigateTo("Subscriptions.qml", {
            mode: "playlist",
            playlistId: pl.playlistId,
            playlistName: pl.title
        }, { currentIndex: itemList.currentIndex })
    }

    // Straight to the player in shuffle mode — it loads the playlist itself, so
    // Back lands here rather than on an item list.
    function openShuffled(pl) {
        playlistsRoot.navigateTo("Player.qml", {
            playlistId: pl.playlistId,
            playlistName: pl.title
        }, { currentIndex: itemList.currentIndex })
    }

    function activateCurrent() {
        var pl = items[itemList.currentIndex]
        if (!pl)
            return
        var setting = appCore.get_setting(moduleRoot.moduleId, "shuffle_playlists") || "Ask"
        if (setting === "Always")
            openShuffled(pl)
        else if (setting === "Never")
            openInOrder(pl)
        else {
            shuffleChoiceIndex = 0
            shuffleOverlayVisible = true
        }
    }

    function applyShuffleChoice() {
        var pl = items[itemList.currentIndex]
        shuffleOverlayVisible = false
        if (!pl)
            return
        if (shuffleChoiceIndex === 1)
            openShuffled(pl)
        else
            openInOrder(pl)
    }

    Component.onCompleted: {
        isLoading = true
        errorMessage = ""
        youtubeBackend.load_playlists()
    }

    Connections {
        target: youtubeBackend

        function onPlaylistsLoaded(playlists) {
            playlistsRoot.isLoading = false
            playlistsRoot.items = playlists
            if (playlists.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                itemList.currentIndex = Math.min(restore, playlists.length - 1)
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            playlistsRoot.isLoading = false
            playlistsRoot.errorMessage = msg
        }
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Playlists"
    }

    // Loading / empty / error states
    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage !== ""
        text: errorMessage
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        width: root.sw * 0.76875 //492 — long guidance lines wrap instead of clipping offscreen
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage === "" && items.length === 0
        text: "NO PLAYLISTS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Playlist list
    ListView {
        id: itemList
        model: items
        opacity: shuffleOverlayVisible ? 0.3 : 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (shuffleOverlayVisible) {
                shuffleChoiceIndex = (shuffleChoiceIndex === 0) ? 1 : 0
                return
            }
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
        }
        Keys.onDownPressed: {
            if (shuffleOverlayVisible) {
                shuffleChoiceIndex = (shuffleChoiceIndex === 0) ? 1 : 0
                return
            }
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
        }
        Keys.onReturnPressed: {
            if (shuffleOverlayVisible)
                playlistsRoot.applyShuffleChoice()
            else
                playlistsRoot.activateCurrent()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                if (shuffleOverlayVisible)
                    shuffleOverlayVisible = false
                else
                    playlistsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, itemList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: itemList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.title || ""
                    color: itemList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
                }

                SequentialAnimation {
                    running: (itemList.currentIndex === index) &&
                             (rowText.implicitWidth > textClip.width)
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) rowText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: rowText; property: "x"
                        to: textClip.width - rowText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: rowText; property: "x"; value: 0 }
                }
            }
        }
    }

    // Play/Shuffle overlay ([ASK] mode)
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: shuffleOverlayVisible

        Rectangle {
            color: root.surfaceColor
            anchors.centerIn: parent
            width: root.sw * 0.76875
            height: root.sh * 0.2833333

            Column {
                id: shuffleDialogColumn
                anchors.fill: parent
                spacing: root.sh * 0.05

                Text {
                    text: "START PLAYBACK?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: ["Play in order", "Shuffle"]
                        delegate: Item {
                            width: shuffleDialogColumn.width
                            height: root.sh * 0.0583333

                            Rectangle {
                                anchors.fill: shuffleDelegateText
                                color: root.accentColor
                                visible: index === shuffleChoiceIndex
                            }

                            Text {
                                id: shuffleDelegateText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData
                                color: index === shuffleChoiceIndex ? root.surfaceColor : root.primaryColor
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
                    text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    // Footer
    Text {
        id: footer
        visible: !shuffleOverlayVisible
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
