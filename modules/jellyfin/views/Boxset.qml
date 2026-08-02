import QtQuick
import Components

// Box-set detail: lists one row per content type the box set holds
// (MOVIES / SERIES / EPISODES / COLLECTIONS). Selecting a row opens that type's
// items in Items.qml "static" mode. A box set can nest other box sets, so the
// COLLECTIONS row loops back through Items.qml → Boxset.qml.
FocusScope {
    id: boxsetRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal replaceCurrent(string path, var params)
    signal goBack()

    property var item: navParams.item || ({})
    property string libraryName: navParams.libraryName || ""

    property var categories: []
    property bool isLoading: false
    property string errorMessage: ""

    // type → {label, order}. Buckets render in this order; unknown types ignored.
    function categoryFor(type) {
        if (type === "movie")   return { label: "MOVIES",      order: 0 }
        if (type === "series")  return { label: "SERIES",      order: 1 }
        if (type === "episode") return { label: "EPISODES",    order: 2 }
        if (type === "boxset")  return { label: "COLLECTIONS", order: 3 }
        return null
    }

    // Sort a bucket's items by release date, oldest first. Items with a missing
    // or unparseable date sink to the bottom, keeping their incoming (SortName)
    // order relative to one another.
    function sortCollectionItems(itemArr) {
        if (!itemArr) return []
        var result = itemArr.slice()
        result.sort(function(a, b) {
            var av = a && (a.releaseDate || a.ReleaseDate)
            var bv = b && (b.releaseDate || b.ReleaseDate)
            var at = av ? Date.parse(av) : NaN
            var bt = bv ? Date.parse(bv) : NaN
            var aBad = isNaN(at), bBad = isNaN(bt)
            if (aBad && bBad) return 0
            if (aBad) return 1
            if (bBad) return -1
            return at - bt
        })
        return result
    }

    Connections {
        target: jellyfinBackend

        function onBoxsetChildrenLoaded(children) {
            boxsetRoot.isLoading = false

            // Bucket children by content type. Each bucket is re-sorted by
            // release date (oldest first) via sortCollectionItems before handoff.
            var buckets = {}
            for (var i = 0; i < children.length; i++) {
                var c = children[i]
                var meta = boxsetRoot.categoryFor(c.type)
                if (!meta) continue
                if (!buckets[meta.label])
                    buckets[meta.label] = { label: meta.label, order: meta.order, items: [] }
                buckets[meta.label].items.push(c)
            }

            var cats = []
            for (var key in buckets) cats.push(buckets[key])
            cats.sort(function(a, b) { return a.order - b.order })
            boxsetRoot.categories = cats

            // Single content type → skip the category screen and hand straight to
            // that type's list, without leaving this view on the back stack.
            if (cats.length === 1) {
                boxsetRoot.replaceCurrent("Items.qml", {
                    mode: "static",
                    items: boxsetRoot.sortCollectionItems(cats[0].items),
                    title: cats[0].label,
                    libraryName: boxsetRoot.item.title || boxsetRoot.libraryName
                })
                return
            }

            if (cats.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                categoryList.currentIndex = Math.min(restore, cats.length - 1)
                categoryList.positionViewAtIndex(categoryList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            boxsetRoot.isLoading = false
            boxsetRoot.errorMessage = msg
            console.log("[Jellyfin Boxset] Error: " + msg)
        }
    }

    Component.onCompleted: {
        if (item.itemId) {
            isLoading = true
            errorMessage = ""
            jellyfinBackend.load_boxset_children(item.itemId)
        }
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    function selectCategory() {
        var cat = categories[categoryList.currentIndex]
        if (!cat) return
        boxsetRoot.navigateTo("Items.qml", {
            mode: "static",
            items: boxsetRoot.sortCollectionItems(cat.items),
            title: cat.label,
            libraryName: boxsetRoot.item.title || boxsetRoot.libraryName
        }, { currentIndex: categoryList.currentIndex })
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: boxsetRoot.item.title || ""
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
        visible: !isLoading && errorMessage === "" && categories.length === 0
        text: "NO ITEMS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    ListView {
        id: categoryList
        model: categories
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            categoryList.positionViewAtIndex(categoryList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            categoryList.positionViewAtIndex(categoryList.currentIndex, ListView.Contain)
        }
        Keys.onReturnPressed: boxsetRoot.selectCategory()

        delegate: Item {
            width: categoryList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, categoryList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: categoryList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: (modelData.label || "") + " (" + (modelData.items ? modelData.items.length : 0) + ")"
                    color: categoryList.currentIndex === index ? root.surfaceColor : root.primaryColor
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
