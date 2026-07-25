import QtQuick
import Components

// Reusable list browser — handles movies, series, and episodes via navParams.
// Follows the Plex Items.qml pattern: 28px rows, 24px font, marquee on select.
FocusScope {
    id: itemListRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string parentId: navParams.parentId || ""
    property string libraryName: navParams.libraryName || ""
    property string includeTypes: navParams.includeTypes || "Movie"
    property string mode: navParams.mode || "browse"   // "browse", "resume", or "up_next"

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    // A–Z letter-jump panel — only for the alphabetized full-library list
    // ("browse"); resume/up_next are not alpha-sorted.
    property bool showLetterNav: mode === "browse" || mode === "folder" || itemListRoot.mode === "boxset"
    property bool letterNavActive: false
    property var letterIndex: []

    // Sort key for a title: strip a leading article, take the first A–Z char,
    // else group under "#".
    function sortKey(title) {
        var t = (title || "").toLowerCase()
        var articles = ["the ", "a ", "an "]
        for (var i = 0; i < articles.length; i++) {
            if (t.indexOf(articles[i]) === 0) { t = t.substring(articles[i].length); break }
        }
        var ch = t.charAt(0).toUpperCase()
        return (ch >= 'A' && ch <= 'Z') ? ch : '#'
    }

    // Build [{letter, firstIndex}] over the (already alpha-sorted) item list.
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

    Connections {
        target: jellyfinBackend

        function onItemsLoaded(loadedItems) {
            if (itemListRoot.mode !== "browse") return
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

        function onFolderChildrenLoaded(loadedItems) {
            if (itemListRoot.mode !== "folder") return
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

        function onBoxsetChildrenLoaded(loadedItems) {
            if (itemListRoot.mode !== "boxset") return
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

        function onContinueWatchingLoaded(loadedItems) {
            if (itemListRoot.mode !== "resume") return
            itemListRoot.isLoading = false
            itemListRoot.items = loadedItems
            if (loadedItems.length > 0) {
                itemList.currentIndex = 0
                itemList.positionViewAtIndex(0, ListView.Contain)
            }
        }

        function onUpNextLoaded(loadedItems) {
            if (itemListRoot.mode !== "up_next") return
            itemListRoot.isLoading = false
            itemListRoot.items = loadedItems
            if (loadedItems.length > 0) {
                itemList.currentIndex = 0
                itemList.positionViewAtIndex(0, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            itemListRoot.isLoading = false
            itemListRoot.errorMessage = msg
            console.log("[Jellyfin Items] Error: " + msg)
        }
    }

    Component.onCompleted: {
        if (mode === "static") {
            // Pre-filtered list handed over by Boxset.qml — no fetch needed.
            isLoading = false
            errorMessage = ""
            items = navParams.items || []
            if (items.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                itemList.currentIndex = Math.min(restore, items.length - 1)
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
            }
        } else if (mode === "folder") {
            isLoading = true
            errorMessage = ""
            jellyfinBackend.load_folder_children(parentId)
        } else if (mode === "boxset") {
            isLoading = true
            errorMessage = ""
            jellyfinBackend.load_boxset_children(parentId)
        } else if (mode === "resume") {
            isLoading = true
            errorMessage = ""
            jellyfinBackend.load_continue_watching()
        } else if (mode === "up_next") {
            isLoading = true
            errorMessage = ""
            jellyfinBackend.load_up_next()
        } else {
            isLoading = true
            errorMessage = ""
            jellyfinBackend.load_items(parentId, includeTypes, "SortName")
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

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        // libraryName is the persistent section label (= title in every mode
        // except "static", where title is the category name but libraryName is
        // the box-set name) — keeps the box-set name consistent down the chain.
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

    // Body — Plex-style text list
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
        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) {
                currentIndex-- 
                var curLetter = sortKey((items[itemList.currentIndex] && items[itemList.currentIndex].title) || "")
                for (var i = 0; i < letterIndex.length; i++) {
                    if (letterIndex[i].letter === curLetter) { letterList.currentIndex = i; break }
                }
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
            }
            else {
                currentIndex = count-1
                itemList.positionViewAtIndex(currentIndex, ListView.Contain)
                letterList.currentIndex = letterIndex.length-1
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
            }
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) {
                currentIndex++
                var curLetter = sortKey((items[itemList.currentIndex] && items[itemList.currentIndex].title) || "")
                for (var i = 0; i < letterIndex.length; i++) {
                    if (letterIndex[i].letter === curLetter) { letterList.currentIndex = i; break }
                }
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
            }
            else {
                currentIndex = 0
                itemList.positionViewAtIndex(currentIndex, ListView.Contain)
                letterList.currentIndex = 0
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
            }
        }
        Keys.onReturnPressed: itemListRoot.selectItem()
        Keys.onPressed: function(event) {
            // Right hands focus to the letter panel, synced to the current letter.
            if (event.key === Qt.Key_Right && showLetterNav && letterIndex.length > 0) {
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
                        if (modelData.type === "episode" && modelData.grandparentTitle) {
                            var sNum = (modelData.parentIndex != null) ? modelData.parentIndex : "?"
                            var eNum = (modelData.index || modelData.index === 0) ? modelData.index : "?"
                            return modelData.grandparentTitle + " S" + sNum + "E" + eNum + ": " + (modelData.title || "")
                        }
                        var base = modelData.title || ""
                        return modelData.year ? base + " (" + String(modelData.year) + ")" : base
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

    // A–Z letter navigation panel
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
            if (count === 0) return
            if (currentIndex > 0) {
                currentIndex-- 
            }
            else {
                currentIndex = count - 1
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Beginning)
            }
            itemList.currentIndex = letterIndex[currentIndex].firstIndex
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Beginning)
            
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) {
                currentIndex++
            }
            else {
                currentIndex = 0
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Beginning)
            }
            itemList.currentIndex = letterIndex[currentIndex].firstIndex
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Beginning)
        }
        Keys.onReturnPressed: { letterNavActive = false; itemList.forceActiveFocus() }
        Keys.onLeftPressed:   { letterNavActive = false; itemList.forceActiveFocus() }
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

    function selectItem() {
        var item = items[itemList.currentIndex]
        if (!item) return

        if (item.type === "series") {
            itemListRoot.navigateTo("ItemShow.qml", { item: item, libraryName: libraryName }, { currentIndex: itemList.currentIndex })
        } else if (item.type === "boxset") {
            itemListRoot.navigateTo("Boxset.qml", { item: item, libraryName: libraryName }, { currentIndex: itemList.currentIndex })
        } else if (item.isFolder) {
            // homevideos folder — drill into its direct children, keeping the
            // persistent library name in the header.
            itemListRoot.navigateTo("Items.qml", {
                parentId: item.itemId,
                libraryName: libraryName,
                mode: "folder"
            }, { currentIndex: itemList.currentIndex })
        } else {
            itemListRoot.navigateTo("Item.qml", { item: item, libraryName: libraryName },
                                   { currentIndex: itemList.currentIndex })
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
