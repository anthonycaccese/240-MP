import QtQuick
import Components

// Series landing — show context + season list. Mirrors the Plex ItemShow flow:
// ItemShow → ItemSeason → Item.
FocusScope {
    id: showRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var item: navParams.item || {}
    property string libraryName: navParams.libraryName || ""

    property var seasons: []
    property bool isLoading: false

    // Focus rows: 0 = play button, 1 = season list
    property int focusRow: 0

    // PLAY flow state: waiting for the series' next-up episode, then (on empty)
    // for the first season's episodes as a fallback.
    property bool playPending: false
    property bool fallbackPlay: false

    Connections {
        target: jellyfinBackend

        function onSeasonsLoaded(loadedItems) {
            showRoot.isLoading = false
            showRoot.seasons = loadedItems
            if (loadedItems.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                seasonList.currentIndex = Math.min(restore, loadedItems.length - 1)
                if (navListState.focusRow === 1) focusRow = 1
                seasonList.positionViewAtIndex(seasonList.currentIndex, ListView.Contain)
            }
        }

        // PLAY: the server's resume-or-next-unwatched episode for this series.
        function onSeriesNextUpReady(detail) {
            if (!showRoot.playPending) return
            if (detail && detail.itemId) {
                showRoot.playPending = false
                showRoot.navigateTo("Item.qml", { item: detail, libraryName: showRoot.libraryName }, {})
            } else {
                // Never-started series → fall back to the first real season's first episode.
                showRoot.fallbackPlay = true
                var s0 = null
                for (var i = 0; i < showRoot.seasons.length; i++) {
                    if (showRoot.seasons[i].index >= 1) { s0 = showRoot.seasons[i]; break }
                }
                if (!s0) s0 = showRoot.seasons[0]
                if (s0) jellyfinBackend.load_episodes(showRoot.item.itemId, s0.itemId)
                else { showRoot.playPending = false }
            }
        }

        function onEpisodesLoaded(eps) {
            if (!showRoot.fallbackPlay) return
            showRoot.fallbackPlay = false
            showRoot.playPending = false
            if (eps.length > 0)
                showRoot.navigateTo("Item.qml", { item: eps[0], libraryName: showRoot.libraryName }, {})
        }

        function onErrorOccurred(msg) {
            showRoot.isLoading = false
            showRoot.playPending = false
            showRoot.fallbackPlay = false
            console.log("[Jellyfin Show] Error: " + msg)
        }
    }

    Component.onCompleted: {
        isLoading = true
        focusRow = 0
        if (item.itemId) jellyfinBackend.load_seasons(item.itemId)
    }

    focus: true

    Keys.onUpPressed: {
        if (focusRow === 0) {
            seasonList.currentIndex = seasons.length-1
            focusRow = 1
        } else {
            if (seasons.length === 0) return
            if (focusRow === 1) {
                if (seasonList.currentIndex > 0) seasonList.currentIndex--
                else focusRow = 0
            } 
        }
        seasonList.positionViewAtIndex(seasonList.currentIndex, ListView.Contain)
    }
    Keys.onDownPressed: {
        if (focusRow === 0) {
            seasonList.currentIndex = 0
            if (seasons.length > 0) focusRow = 1
        } else {
            if (seasons.length === 0) return
            if (seasonList.currentIndex < seasons.length - 1) seasonList.currentIndex++
            else seasonList.currentIndex = focusRow = 0
        }
        seasonList.positionViewAtIndex(seasonList.currentIndex, ListView.Contain)
    }
    Keys.onReturnPressed: {
        if (focusRow === 0) {
            if (seasons.length === 0 || playPending) return
            playPending = true
            jellyfinBackend.load_series_next_up(item.itemId)
        } else {
            var season = seasons[seasonList.currentIndex]
            if (!season) return
            showRoot.navigateTo("ItemSeason.qml", {
                item: season,
                seriesId: item.itemId,
                showTitle: item.title,
                libraryName: libraryName
            }, { currentIndex: seasonList.currentIndex, focusRow: 1 })
        }
    }
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
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: libraryName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    Item {
        visible: !isLoading
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true

        Row {
            id: showDetails
            height: root.sh * 0.2916667 //140
            spacing: root.sw * 0.0375 //24

            // PLAY / RSUM button
            Rectangle {
                id: playButton
                color: focusRow === 0 ? root.accentColor : root.surfaceColor
                border.color: focusRow === 0 ? root.accentColor : root.tertiaryColor
                width: root.sw * 0.1875 //120
                height: root.sh * 0.1166667 //56
                border.width: root.sh * 0.003125 //2

                Text {
                    anchors.centerIn: parent
                    text: (item.viewOffset && item.viewOffset > 0) ? "RSUM ►" : "PLAY ►"
                    color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.05 //24
                }
            }

            Column {
                topPadding: root.sh * 0.0083333 //4
                width: root.sw * 0.54375 //348
                spacing: root.sh * 0.0166667 //8

                // Name
                Text {
                    text: item.title || ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.05 //24
                }

                // Year & season count
                Text {
                    text: {
                        var parts = []
                        var sc = seasons.length
                        if (item.year) parts.push(String(item.year))
                        if (sc > 0) parts.push(sc + (sc === 1 ? " SEASON" : " SEASONS"))
                        return parts.join(" - ")
                    }
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.0333333 //16
                }

                Item {
                    id: summaryContainer
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: root.sh * 0.1375 //66
                    clip: true

                    Text {
                        id: summaryText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        text: item.overview || ""
                        color: root.primaryColor
                        font.family: root.globalFont
                        wrapMode: Text.WordWrap
                        font.pixelSize: root.sh * 0.0291667 //14
                        lineHeight: 1.3
                    }

                    SequentialAnimation {
                        running: (item.overview || "") !== "" && summaryText.implicitHeight > summaryContainer.height
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) summaryText.y = 0
                        PauseAnimation { duration: 3000 }
                        NumberAnimation {
                            target: summaryText; property: "y"
                            to: summaryContainer.height - summaryText.implicitHeight
                            duration: Math.abs(to) * 120
                        }
                        PauseAnimation { duration: 4000 }
                        PropertyAction { target: summaryText; property: "y"; value: 0 }
                    }
                }
            }
        }

        // Seasons
        Text {
            id: seasonListLabel
            anchors.top: showDetails.bottom
            text: "Seasons:"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.topMargin: root.sh * 0.0145833 //7
            leftPadding: root.sw * 0.009375 //6
            rightPadding: root.sw * 0.009375 //6
            font.pixelSize: root.sh * 0.0291667 //14
        }

        ListView {
            id: seasonList
            model: seasons
            anchors.top: seasonListLabel.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: root.sh * 0.0145833 //7
            height: root.sh * 0.175 //84
            clip: true

            delegate: Item {
                width: seasonList.width
                height: root.sh * 0.0583333 //28

                Item {
                    id: textClip
                    width: Math.min(rowText.implicitWidth, seasonList.width)
                    height: parent.height
                    clip: true

                    Rectangle {
                        color: root.accentColor
                        anchors.fill: rowText
                        visible: seasonList.currentIndex === index && focusRow === 1
                    }

                    Text {
                        id: rowText
                        text: {
                            var label = modelData.title || ("Season " + modelData.index)
                            var count = modelData.leafCount || 0
                            if (count > 0) label += " (" + count + " episodes)"
                            return label
                        }
                        color: (seasonList.currentIndex === index && focusRow === 1)
                               ? root.surfaceColor : root.primaryColor
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
                        running: (seasonList.currentIndex === index) &&
                                 (focusRow === 1) &&
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
    }

    // Footer
    Text {
        id: footer
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
