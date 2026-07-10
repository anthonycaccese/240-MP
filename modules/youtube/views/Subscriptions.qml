import QtQuick
import Components

// Video list — serves the aggregated Subscriptions feed (mode "feed"), a single
// channel's videos (mode "channel"), a user playlist ("playlist"), the saved
// Watch Later list ("watchlater") and the recently-watched History list
// ("history"). Right on any row opens the save/remove watch-later overlay.
FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string mode: navParams.mode || "feed"
    property string channelId: navParams.channelId || ""
    property string channelName: navParams.channelName || ""
    property string playlistId: navParams.playlistId || ""
    property string playlistName: navParams.playlistName || ""

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    // Watch-later overlay state
    property bool wlOverlayVisible: false
    property bool wlRemoveMode: false
    property int wlChoiceIndex: 0   // 0 = yes, 1 = no

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    function restoreListIndex() {
        if (items.length === 0)
            return
        var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
        itemList.currentIndex = Math.min(restore, items.length - 1)
        itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
    }

    function openWatchLaterOverlay() {
        var it = items[itemList.currentIndex]
        if (!it)
            return
        wlRemoveMode = (mode === "watchlater") || youtubeBackend.isInWatchLater(it.videoId)
        wlChoiceIndex = 0
        wlOverlayVisible = true
    }

    function applyWatchLaterChoice() {
        if (wlChoiceIndex === 0) {
            var it = items[itemList.currentIndex]
            if (it && wlRemoveMode) {
                youtubeBackend.removeFromWatchLater(it.videoId)
                if (mode === "watchlater") {
                    var idx = itemList.currentIndex
                    items = youtubeBackend.getWatchLater()
                    itemList.currentIndex = Math.min(idx, Math.max(0, items.length - 1))
                }
            } else if (it) {
                youtubeBackend.addToWatchLater(it.videoId, it.title || "", it.channelName || "")
            }
        }
        wlOverlayVisible = false
    }

    // Hide Shorts from feed/channel lists when the "Display Shorts" toggle is off.
    // Unset/true/"ON" => show shorts (default); explicit false => hide.
    function filterShorts(videos) {
        var raw = appCore.get_setting(moduleRoot.moduleId, "display_shorts")
        var showShorts = (raw === undefined || raw === null) ? true
                         : (raw === true || raw === "ON")
        if (showShorts)
            return videos
        return videos.filter(function(v) { return !v.isShort })
    }

    Component.onCompleted: {
        if (mode === "watchlater") {
            items = youtubeBackend.getWatchLater()
            restoreListIndex()
        } else if (mode === "history") {
            items = youtubeBackend.getHistory()
            restoreListIndex()
        } else {
            isLoading = true
            errorMessage = ""
            if (mode === "feed")
                youtubeBackend.load_subscriptions_feed()
            else if (mode === "playlist")
                youtubeBackend.load_playlist_videos(playlistId)
            else
                youtubeBackend.load_channel_videos(channelId)
        }
    }

    Connections {
        target: youtubeBackend

        function onSubscriptionsFeedLoaded(videos) {
            if (itemsRoot.mode !== "feed")
                return
            itemsRoot.isLoading = false
            itemsRoot.items = itemsRoot.filterShorts(videos)
            itemsRoot.restoreListIndex()
        }

        function onChannelVideosLoaded(loadedChannelId, videos) {
            if (itemsRoot.mode !== "channel" || loadedChannelId !== itemsRoot.channelId)
                return
            itemsRoot.isLoading = false
            itemsRoot.items = itemsRoot.filterShorts(videos)
            itemsRoot.restoreListIndex()
        }

        function onPlaylistVideosLoaded(loadedPlaylistId, videos) {
            if (itemsRoot.mode !== "playlist" || loadedPlaylistId !== itemsRoot.playlistId)
                return
            itemsRoot.isLoading = false
            itemsRoot.items = itemsRoot.filterShorts(videos)
            itemsRoot.restoreListIndex()
        }

        function onErrorOccurred(msg) {
            if (itemsRoot.mode !== "feed" && itemsRoot.mode !== "channel" && itemsRoot.mode !== "playlist")
                return
            itemsRoot.isLoading = false
            itemsRoot.errorMessage = msg
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
        subtitle: itemsRoot.mode === "feed" ? "Subscriptions"
                : itemsRoot.mode === "watchlater" ? "Watch Later"
                : itemsRoot.mode === "history" ? "History"
                : itemsRoot.mode === "playlist" ? itemsRoot.playlistName
                : itemsRoot.channelName
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
        text: "NO VIDEOS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // List
    ListView {
        id: itemList
        model: itemsRoot.items
        opacity: wlOverlayVisible ? 0.3 : 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: true

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.075 //36

            // Full-width background highlight for the active row
            Rectangle {
                color: root.accentColor
                anchors.fill: parent
                visible: itemList.currentIndex === index
            }

            // Vertical stack for Subtitle and Title
            Column {
                id: textColumn
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0109375 //7
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.sh * 0.0041667 //2

                Text {
                    id: subtitleLabel
                    text: modelData.channelName || ""
                    color: itemList.currentIndex === index ? root.surfaceColor : root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0208333 //10
                }

                // Clipped title with marquee scroll when it overflows the row
                Item {
                    id: titleClip
                    width: Math.min(titleLabel.implicitWidth, itemList.width - root.sw * 0.021875) //14
                    height: titleLabel.implicitHeight
                    clip: true

                    Text {
                        id: titleLabel
                        text: modelData.title || ""
                        color: itemList.currentIndex === index ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0333333 //16
                        x: 0
                    }

                    SequentialAnimation {
                        running: (itemList.currentIndex === index) &&
                                 (titleLabel.implicitWidth > titleClip.width)
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) titleLabel.x = 0
                        PauseAnimation { duration: 1500 }
                        NumberAnimation {
                            target: titleLabel; property: "x"
                            to: titleClip.width - titleLabel.implicitWidth
                            duration: Math.abs(to) * 20
                        }
                        PauseAnimation { duration: 2000 }
                        PropertyAction { target: titleLabel; property: "x"; value: 0 }
                    }
                }
            }
        }

        Keys.onReturnPressed: {
            if (wlOverlayVisible) {
                applyWatchLaterChoice()
                return
            }
            var selected = itemsRoot.items[itemList.currentIndex]
            if (!selected)
                return
            navigateTo("Player.qml", { item: selected }, { currentIndex: itemList.currentIndex })
        }
        Keys.onPressed: function(event) {
            if (wlOverlayVisible) {
                if (event.key === Qt.Key_Up) {
                    wlChoiceIndex = 0
                    event.accepted = true
                } else if (event.key === Qt.Key_Down) {
                    wlChoiceIndex = 1
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                    wlOverlayVisible = false
                    event.accepted = true
                }
                return
            }
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                itemsRoot.goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                openWatchLaterOverlay()
                event.accepted = true
            }
        }
    }

    // Watch-later save/remove overlay
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: wlOverlayVisible

        Rectangle {
            color: root.surfaceColor
            anchors.centerIn: parent
            width: root.sw * 0.76875
            height: root.sh * 0.2833333

            Column {
                id: wlDialogColumn
                anchors.fill: parent
                spacing: root.sh * 0.05

                Text {
                    text: wlRemoveMode ? "REMOVE FROM WATCH LATER?" : "SAVE TO WATCH LATER?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: ["Yes", "No"]
                        delegate: Item {
                            width: wlDialogColumn.width
                            height: root.sh * 0.0583333

                            Rectangle {
                                anchors.fill: wlDelegateText
                                color: root.accentColor
                                visible: index === wlChoiceIndex
                            }

                            Text {
                                id: wlDelegateText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData
                                color: index === wlChoiceIndex ? root.surfaceColor : root.primaryColor
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
        visible: !wlOverlayVisible
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + root.hints.browse + (itemsRoot.mode === "watchlater" ? ":REMOVE " : ":SAVE ")
              + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
