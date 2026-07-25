import QtQuick
import Components

FocusScope {
    id: seasonRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var item: navParams.item || {}
    property string showTitle: navParams.showTitle || item.grandparentTitle || ""
    property string libraryName: navParams.libraryName || ""

    property var episodes: []
    property bool isLoading: false

    // Extras attached to this season (trailers, behind the scenes, …)
    property var extras: []
    readonly property bool hasExtras: extras.length > 0

    // Focus rows: 0 = play button, 1 = extras (when hasExtras), 2 = episode list
    property int focusRow: 0

    Connections {
        target: plexBackend

        function onChildrenLoaded(loadedItems) {
            seasonRoot.isLoading = false
            seasonRoot.episodes = loadedItems
            if (loadedItems.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                episodeList.currentIndex = Math.min(restore, loadedItems.length - 1)
                if (navListState.focusRow === 2) focusRow = 2
                episodeList.positionViewAtIndex(episodeList.currentIndex, ListView.Contain)
            }
        }

        function onExtrasLoaded(items) {
            seasonRoot.extras = items
            if (navListState.focusRow === 1 && items.length > 0) focusRow = 1
        }

        function onErrorOccurred(msg) {
            seasonRoot.isLoading = false
            console.log("[SeasonItem] Error: " + msg)
        }
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

    // Season label as displayed in the header line ("SPECIALS" / "SEASON n")
    function seasonLabel() {
        if (item.index === 0) return "Specials"
        if (item.index) return "Season " + item.index
        return ""
    }

    Keys.onUpPressed: {
        if (focusRow === 2) {
            if (episodeList.currentIndex > 0) {
                episodeList.currentIndex--
            } else {
                focusRow = hasExtras ? 1 : 0
            }
        } else if (focusRow === 1) {
            focusRow = 0
        } else {
            if (episodes.length > 0) {
                episodeList.currentIndex = episodes.length - 1
                focusRow = 2
            } else if (hasExtras) {
                focusRow = 1
            }
        }
        episodeList.positionViewAtIndex(episodeList.currentIndex, ListView.Contain)
    }
    Keys.onDownPressed: {
        if (focusRow === 0) {
            if (hasExtras) focusRow = 1
            else if (episodes.length > 0) {
                episodeList.currentIndex = 0
                focusRow = 2
            }
        } else if (focusRow === 1) {
            if (episodes.length > 0) {
                episodeList.currentIndex = 0
                focusRow = 2
            } else {
                focusRow = 0
            }
        } else {
            if (episodeList.currentIndex < episodes.length - 1) {
                episodeList.currentIndex++
            } else {
                episodeList.currentIndex = 0
                focusRow = 0
            }
        }
        episodeList.positionViewAtIndex(episodeList.currentIndex, ListView.Contain)
    }
    Keys.onReturnPressed: {
        if (focusRow === 0) {
            playBestEpisode()
        } else if (focusRow === 1) {
            var label = seasonLabel()
            seasonRoot.navigateTo("Extras.qml", {
                extras: extras,
                ratingKey: item.ratingKey,
                itemTitle: showTitle + (label ? " - " + label : ""),
                libraryName: libraryName
            }, { focusRow: 1 })
        } else {
            var ep = episodes[episodeList.currentIndex]
            if (!ep) return
            seasonRoot.navigateTo("Item.qml", {
                item: ep,
                libraryName: libraryName
            }, { currentIndex: episodeList.currentIndex, focusRow: 2 })
        }
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    function playBestEpisode() {
        if (episodes.length === 0) return
        var target = null
        // 1. In-progress episode (viewOffset > 0)
        for (var i = 0; i < episodes.length; i++) {
            if (episodes[i].viewOffset > 0) { target = episodes[i]; break }
        }
        // 2. First unwatched episode (viewCount === 0)
        if (!target) {
            for (var j = 0; j < episodes.length; j++) {
                if (!episodes[j].viewCount || episodes[j].viewCount === 0) { target = episodes[j]; break }
            }
        }
        // 3. All watched — start from beginning
        if (!target) {
            target = episodes[0]
        }
        var ep = Object.assign({}, target)
        // For cases 2 and 3 ensure we start from beginning
        if (!ep.viewOffset) ep.viewOffset = 0
        seasonRoot.navigateTo("Item.qml", {
            item: ep,
            libraryName: libraryName
        }, {})
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
            id: seasonDetails
            height: root.sh * 0.175 //84
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
                        text: {
                            // Show RSUM if any in-progress episode exists
                            for (var i = 0; i < seasonRoot.episodes.length; i++) {
                                if (seasonRoot.episodes[i].viewOffset > 0) return "RSUM \u25BA"
                            }
                            return "PLAY \u25BA"
                        }
                        color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.pixelSize: root.sh * 0.05 //24
                    }
                }

                // Extras Button
                Rectangle {
                    id: extrasButton
                    visible: seasonRoot.hasExtras
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

                Text {
                    text: showTitle
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.05 //24
                }

                Text {
                    text: {
                        var parts = []
                        if (item.index === 0) parts.push("Specials")
                        else if (item.index) parts.push("SEASON " + item.index)
                        var yr = item.originallyAvailableAt
                                 ? item.originallyAvailableAt.substring(0, 4)
                                 : (item.year ? String(item.year) : "")
                        if (yr) parts.push(yr)
                        return parts.join(" - ")
                    }
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.0333333 //16
                }
            }
        }

        // Seasons
        Text {
            id: episodeListLabel
            anchors.top: seasonDetails.bottom
            text: "Episodes:"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.topMargin: root.sh * 0.0145833 //7
            leftPadding: root.sw * 0.009375 //6;
            rightPadding: root.sw * 0.009375 //6;
            font.pixelSize: root.sh * 0.0291667 //14
        }

        ListView {
            id: episodeList
            model: episodes
            anchors.top: episodeListLabel.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: root.sh * 0.0145833 //7
            height: root.sh * 0.2916667 //140
            clip: true

            delegate: Item {
                width: episodeList.width
                height: root.sh * 0.0583333 //28

                Item {
                    id: textClip
                    width: Math.min(rowText.implicitWidth, episodeList.width)
                    height: parent.height
                    clip: true

                    Rectangle {
                        color: root.accentColor
                        anchors.fill: rowText
                        visible: episodeList.currentIndex === index && focusRow === 2
                    }

                    Text {
                        id: rowText
                        text: {
                            var s = (modelData.parentIndex != null) ? ("S" + modelData.parentIndex) : ""
                            var e = modelData.index ? ("E" + modelData.index) : ""
                            var prefix = (s || e) ? (s + e + ": ") : ""
                            return prefix + (modelData.title || "")
                        }
                        color: (episodeList.currentIndex === index && focusRow === 2)
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
                        running: (episodeList.currentIndex === index) &&
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
