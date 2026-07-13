import QtQuick
import Components

FocusScope {
    id: showRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var item: navParams.item || {}
    property string libraryName: navParams.libraryName || ""

    property var seasons: []
    property bool isLoading: false

    // Extras attached to this show (trailers, behind the scenes, …)
    property var extras: []
    readonly property bool hasExtras: extras.length > 0

    // Focus rows: 0 = play button, 1 = extras (when hasExtras), 2 = season list
    property int focusRow: 0

    // When true, the next childrenLoaded signal carries episodes to play, not seasons to display
    property bool playOnLoad: false
    property bool waitingForOnDeck: false

    Connections {
        target: plexBackend

        function onChildrenLoaded(loadedItems) {
            if (showRoot.playOnLoad) {
                showRoot.playOnLoad = false
                showRoot.playBestEpisodeFromList(loadedItems)
            } else {
                showRoot.isLoading = false
                showRoot.seasons = loadedItems
                if (loadedItems.length > 0) seasonList.currentIndex = 0
            }
        }

        function onExtrasLoaded(items) {
            showRoot.extras = items
        }

        function onInProgressEpisodeLoaded(episodeItem) {
            if (!showRoot.waitingForOnDeck) return
            showRoot.waitingForOnDeck = false
            if (episodeItem && episodeItem.ratingKey) {
                // Found an in-progress episode — RSUM it directly
                showRoot.navigateTo("Item.qml", { item: episodeItem, libraryName: showRoot.libraryName }, {})
            } else {
                // No in-progress episode — fall back to first-unwatched logic
                showRoot.startPlayFallback()
            }
        }

        function onErrorOccurred(msg) {
            showRoot.isLoading = false
            showRoot.playOnLoad = false
            showRoot.waitingForOnDeck = false
            console.log("[ShowItem] Error: " + msg)
        }
    }

    function startPlayFallback() {
        if (seasons.length === 0) return
        var targetSeason = null
        for (var i = 0; i < seasons.length; i++) {
            if (seasons[i].viewedLeafCount < seasons[i].leafCount) {
                targetSeason = seasons[i]; break
            }
        }
        if (!targetSeason) targetSeason = seasons[0]
        showRoot.playOnLoad = true
        plexBackend.load_children(targetSeason.ratingKey)
    }

    function playBestEpisodeFromList(epList) {
        if (epList.length === 0) return
        var target = null
        for (var i = 0; i < epList.length; i++) {
            if (epList[i].viewOffset > 0) { target = epList[i]; break }
        }
        if (!target) {
            for (var j = 0; j < epList.length; j++) {
                if (!epList[j].viewCount || epList[j].viewCount === 0) { target = epList[j]; break }
            }
        }
        if (!target) target = epList[0]
        showRoot.navigateTo("Item.qml", { item: target, libraryName: libraryName }, {})
    }

    Component.onCompleted: {
        isLoading = true
        focusRow = 0
        if (item.ratingKey) {
            plexBackend.load_children(item.ratingKey)
            plexBackend.load_extras(item.ratingKey)
        }
    }

    focus: true

    Keys.onUpPressed: {
        if (focusRow === 2) {
            if (seasonList.currentIndex > 0) {
                seasonList.currentIndex--
            } else {
                focusRow = hasExtras ? 1 : 0
            }
        } else if (focusRow === 1) {
            focusRow = 0
        }
    }
    Keys.onDownPressed: {
        if (focusRow === 0) {
            if (hasExtras) focusRow = 1
            else if (seasons.length > 0) focusRow = 2
        } else if (focusRow === 1) {
            if (seasons.length > 0) focusRow = 2
        } else {
            if (seasonList.currentIndex < seasons.length - 1)
                seasonList.currentIndex++
        }
    }
    Keys.onReturnPressed: {
        if (focusRow === 0) {
            if (seasons.length === 0) return
            showRoot.waitingForOnDeck = true
            plexBackend.load_on_deck_for(item.ratingKey)
        } else if (focusRow === 1) {
            showRoot.navigateTo("Extras.qml", {
                extras: extras,
                ratingKey: item.ratingKey,
                itemTitle: item.title,
                libraryName: libraryName
            }, {})
        } else {
            var season = seasons[seasonList.currentIndex]
            if (!season) return
            showRoot.navigateTo("ItemSeason.qml", {
                item: season,
                showTitle: item.title,
                libraryName: libraryName
            }, { currentIndex: seasonList.currentIndex })
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

            Column {
                width: root.sw * 0.1875 //120

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
                        text: (item.viewOffset && item.viewOffset > 0) ? "RSUM \u25BA" : "PLAY \u25BA"
                        color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.pixelSize: root.sh * 0.05 //24
                    }
                }

                // Extras Button
                Rectangle {
                    id: extrasButton
                    visible: showRoot.hasExtras
                    color: focusRow === 1 ? root.accentColor : "transparent"
                    width: parent.width
                    height: extrasLabel.implicitHeight + root.sh * 0.025 //12
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        id: extrasLabel
                        text: "VIEW EXTRAS"
                        color: focusRow === 1 ? root.surfaceColor : root.secondaryColor
                        font.family: root.globalFont
                        anchors.centerIn: parent
                        font.pixelSize: root.sh * 0.025 //12
                    }
                }
            }

            Column {
                topPadding: root.sh * 0.0083333 //4
                width: root.sw * 0.54375 //348
                spacing: root.sh * 0.0166667 //8

                //Name
                Text {
                    text: item.title || ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.05 //24
                }

                // Seasons & Year
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
                        text: item.summary || ""
                        color: root.primaryColor
                        font.family: root.globalFont
                        wrapMode: Text.WordWrap
                        font.pixelSize: root.sh * 0.0291667 //14
                        lineHeight: 1.3
                    }

                    SequentialAnimation {
                        running: item.summary !== null && summaryText.implicitHeight > summaryContainer.height
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
            leftPadding: root.sw * 0.009375 //6;
            rightPadding: root.sw * 0.009375 //6;
            font.pixelSize: root.sh * 0.0291667 //14
        }

        // Season List
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
                        visible: seasonList.currentIndex === index && focusRow === 2
                    }

                    Text {
                        id: rowText
                        text: modelData.title || ""
                        color: (seasonList.currentIndex === index && focusRow === 2)
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
                                 (focusRow === 2) &&
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
