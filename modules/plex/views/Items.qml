import QtQuick
import Components

// Reusable list view — handles all listType values via navParams.
FocusScope {
    id: itemListRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string listType: navParams.listType || ""
    property string listTitle: navParams.title || ""
    property string sectionId: navParams.sectionId || ""
    property string hubKey: navParams.hubKey || ""
    property string ratingKey: navParams.ratingKey || ""
    property string categoryKey: navParams.categoryKey || ""
    property string libraryName: navParams.libraryName || ""

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    property bool showLetterNav: listType === "library_all"
    property bool letterNavActive: false
    property var letterIndex: []

    function sortKey(title) {
        var t = (title || "").toLowerCase()
        var articles = ["the ", "a ", "an "]
        for (var i = 0; i < articles.length; i++) {
            if (t.indexOf(articles[i]) === 0) { t = t.substring(articles[i].length); break }
        }
        var ch = t.charAt(0).toUpperCase()
        return (ch >= 'A' && ch <= 'Z') ? ch : '#'
    }

    function buildLetterIndex(itemArr) {
        var seen = {}
        var result = []
        for (var i = 0; i < itemArr.length; i++) {
            var letter = sortKey(itemArr[i].title || "")
            if (!seen[letter]) {
                seen[letter] = true
                result.push({ letter: letter, firstIndex: i })
            }
        }
        result.sort(function(a, b) {
            if (a.letter === '#') return -1
            if (b.letter === '#') return 1
            return a.letter < b.letter ? -1 : 1
        })
        return result
    }

    // ----------------------------------------------------------------
    // Signal connections from backend
    // ----------------------------------------------------------------

    Connections {
        target: plexBackend

        function onItemsLoaded(loadedItems) {
            var consuming = ["library_all", "hub_items", "collection_items",
                             "playlist_items", "category_items", "continue_watching"]
            if (consuming.indexOf(itemListRoot.listType) >= 0) {
                itemListRoot.isLoading = false
                itemListRoot.items = loadedItems
                if (itemListRoot.showLetterNav)
                    itemListRoot.letterIndex = itemListRoot.buildLetterIndex(loadedItems)
                if (loadedItems.length > 0) {
                    var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                    itemList.currentIndex = Math.min(restore, loadedItems.length - 1)
                    itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
                }
            }
        }

        function onContinueWatchingLoaded(loadedItems) {
            if (itemListRoot.listType === "continue_watching") {
                itemListRoot.isLoading = false
                itemListRoot.items = loadedItems
                if (loadedItems.length > 0) {
                    var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                    itemList.currentIndex = Math.min(restore, loadedItems.length - 1)
                    itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
                }
            }
        }

        function onHubsLoaded(loadedHubs) {
            if (itemListRoot.listType === "hubs") {
                itemListRoot.isLoading = false
                itemListRoot.items = loadedHubs
                if (loadedHubs.length > 0) {
                    var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                    itemList.currentIndex = Math.min(restore, loadedHubs.length - 1)
                    itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
                }
            }
        }

        function onCollectionsLoaded(loadedItems) {
            if (itemListRoot.listType === "collections") {
                itemListRoot.isLoading = false
                itemListRoot.items = loadedItems
                if (itemListRoot.showLetterNav)
                    itemListRoot.letterIndex = itemListRoot.buildLetterIndex(loadedItems)
                if (loadedItems.length > 0) {
                    var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                    itemList.currentIndex = Math.min(restore, loadedItems.length - 1)
                    itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
                }
            }
        }

        function onPlaylistsLoaded(loadedItems) {
            if (itemListRoot.listType === "playlists") {
                itemListRoot.isLoading = false
                itemListRoot.items = loadedItems
                if (itemListRoot.showLetterNav)
                    itemListRoot.letterIndex = itemListRoot.buildLetterIndex(loadedItems)
                if (loadedItems.length > 0) {
                    var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                    itemList.currentIndex = Math.min(restore, loadedItems.length - 1)
                    itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
                }
            }
        }

        function onCategoriesLoaded(loadedItems) {
            if (itemListRoot.listType === "categories") {
                itemListRoot.isLoading = false
                itemListRoot.items = loadedItems
                if (loadedItems.length > 0) {
                    var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                    itemList.currentIndex = Math.min(restore, loadedItems.length - 1)
                    itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
                }
            }
        }

        function onErrorOccurred(msg) {
            if (itemListRoot.listType !== "") {
                itemListRoot.isLoading = false
                itemListRoot.errorMessage = msg
            }
            console.log("[ItemList] Error: " + msg)
        }
    }

    // ----------------------------------------------------------------
    // Select item — navigate based on type
    // ----------------------------------------------------------------

    function selectItem() {
        var item = items[itemList.currentIndex]
        if (!item) return

        // Intermediate lists that navigate deeper
        if (listType === "hubs") {
            // Hub selected → load items for that hub
            itemListRoot.navigateTo("Items.qml", {
                listType: "hub_items",
                title: item.title,
                hubKey: item.hubKey || item.key,
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        if (listType === "collections") {
            itemListRoot.navigateTo("Items.qml", {
                listType: "collection_items",
                title: item.title,
                ratingKey: item.ratingKey,
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        if (listType === "playlists") {
            itemListRoot.navigateTo("Items.qml", {
                listType: "playlist_items",
                title: item.title,
                ratingKey: item.ratingKey,
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        if (listType === "categories") {
            // Boolean filters (e.g. hdr) have no directory listing — apply directly
            var catKey = (item.filterType === "boolean") ? item.key + "=1" : item.key
            itemListRoot.navigateTo("Items.qml", {
                listType: "category_items",
                title: item.title,
                sectionId: sectionId,
                categoryKey: catKey,
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        // For category_items the items are sub-filter values (e.g. genre names),
        // not actual media. Navigate further if type is 'genre_item'.
        if (item.type === "genre_item") {
            // item.ratingKey is actually the filter value key from the server
            // Use it to load actual media items
            itemListRoot.navigateTo("Items.qml", {
                listType: "category_items",
                title: item.title,
                sectionId: item._sectionId || sectionId,
                categoryKey: item._filterKey + "=" + encodeURIComponent(item.ratingKey),
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        // TV Show → go to Show detail view
        if (item.type === "show") {
            itemListRoot.navigateTo("ItemShow.qml", {
                item: item,
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        // Actual media item (movie, episode, other) → go to detail
        itemListRoot.navigateTo("Item.qml", {
            item: item,
            libraryName: libraryName
        }, { currentIndex: itemList.currentIndex })
    }

    // ----------------------------------------------------------------
    // Data loading on appear
    // ----------------------------------------------------------------

    Component.onCompleted: {
        isLoading = true
        errorMessage = ""
        if (listType === "library_all")
            plexBackend.load_library_all(sectionId)
        else if (listType === "hub_items")
            plexBackend.load_items_for_hub(hubKey)
        else if (listType === "hubs")
            plexBackend.load_section_hubs(sectionId)
        else if (listType === "collections")
            plexBackend.load_collections(sectionId)
        else if (listType === "collection_items")
            plexBackend.load_collection_items(ratingKey)
        else if (listType === "playlists")
            plexBackend.load_playlists(sectionId)
        else if (listType === "playlist_items")
            plexBackend.load_playlist_items(ratingKey)
        else if (listType === "categories")
            plexBackend.load_categories(sectionId)
        else if (listType === "category_items")
            plexBackend.load_category_items(sectionId, categoryKey)
        else if (listType === "continue_watching")
            plexBackend.load_continue_watching()
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

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: libraryName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
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
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage === "" && items.length === 0
        text: "NO ITEMS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    ListView {
        id: itemList
        model: items
        opacity: letterNavActive ? 0.3 : 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: showLetterNav ? root.sw * 0.671875 : root.sw * 0.76875 //430 or 492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: if (currentIndex > 0) currentIndex--
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++
        Keys.onReturnPressed: itemListRoot.selectItem()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                itemListRoot.goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Right && showLetterNav && letterIndex.length > 0) {
                var curLetter = sortKey((items[itemList.currentIndex] && items[itemList.currentIndex].title) || "")
                for (var i = 0; i < letterIndex.length; i++) {
                    if (letterIndex[i].letter === curLetter) { letterList.currentIndex = i; break }
                }
                letterNavActive = true
                letterList.forceActiveFocus()
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
                    visible: itemList.currentIndex === index && !letterNavActive
                }

                Text {
                    id: rowText
                    text: {
                        var base = (modelData.type === "episode" && modelData.grandparentTitle)
                                   ? (modelData.grandparentTitle + ": " + (modelData.title || ""))
                                   : (modelData.title || "")
                        return modelData.editionTitle ? base + " (" + modelData.editionTitle + ")" : base
                    }
                    color: (itemList.currentIndex === index && !letterNavActive)
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

    // Letter navigation panel
    ListView {
        id: letterList
        model: letterIndex
        visible: showLetterNav && letterIndex.length > 0
        opacity: letterNavActive ? 1.0 : 0.3
        anchors.left: itemList.right
        anchors.leftMargin: root.sw * 0.0375 //24
        anchors.top: itemList.top
        width: root.sw * 0.0328125 //21
        height: itemList.height
        clip: true
        focus: false

        Keys.onUpPressed: {
            if (currentIndex > 0) {
                currentIndex--
                itemList.currentIndex = letterIndex[currentIndex].firstIndex
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Beginning)
            }
        }
        Keys.onDownPressed: {
            if (currentIndex < count - 1) {
                currentIndex++
                itemList.currentIndex = letterIndex[currentIndex].firstIndex
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Beginning)
            }
        }
        Keys.onReturnPressed: {
            letterNavActive = false
            itemList.forceActiveFocus()
        }
        Keys.onLeftPressed: {
            letterNavActive = false
            itemList.forceActiveFocus()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                letterNavActive = false
                itemList.forceActiveFocus()
                event.accepted = true
            }
        }

        delegate: Item {
            width: letterList.width
            height: root.sh * 0.04375 //21

            Rectangle {
                color: root.accentColor
                anchors.fill: parent
                visible: letterList.currentIndex === index && letterNavActive
            }

            Text {
                text: modelData.letter
                color: (letterList.currentIndex === index && letterNavActive)
                       ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0354167 //17
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                rightPadding: root.sw * 0.009375 //6
                topPadding: root.sh * 0.0041667 //2
                bottomPadding: root.sh * 0.00625 //3
            }
        }
    }

    // Footer
    Text {
        id: footer
        text: showLetterNav
              ? root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.browse + ":BROWSE " + root.hints.select + ":SELECT"
              : root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
