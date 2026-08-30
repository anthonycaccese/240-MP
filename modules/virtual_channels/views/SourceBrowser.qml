import QtQuick
import Components

FocusScope {
    id: browserRoot
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    // Assigned directly by the app's router and nested in navParams by the
    // module's; this view is reachable both ways, so it reads either.
    property var navListState: navParams.navListState || ({})
    // Where the viewer was before descending into a season list. The list
    // arrives asynchronously, so it is put back when the items land rather
    // than on completion, and only once.
    property int restoreIndex: navListState.currentIndex !== undefined
                               ? navListState.currentIndex : -1
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string kind:          navParams.kind      || "shows"
    property string parentKey:     navParams.parentKey || ""
    property string heading:       navParams.title     || "Choose"
    property int    bookingIndex:  navParams.bookingIndex !== undefined ? navParams.bookingIndex : -1
    readonly property bool bookingMode: bookingIndex >= 0
    readonly property string bookingField: kind === "moviegenres"      ? "genres"
                                         : kind === "moviecollections" ? "collections"
                                         : kind === "movieplaylists"   ? "playlists"
                                                                       : "titles"

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var items: []
    property bool loading: true
    property string status: ""

    property var cfg: ({})

    readonly property bool isExclusionLevel: kind === "seasons" || kind === "episodes"
    readonly property string listField: kind === "collections" ? "collections"
                                      : kind === "playlists"   ? "playlists"
                                      : "match"
    readonly property bool canDescend: kind === "shows" || kind === "seasons"

    focus: true

    property var bookingTitles: []

    function reload() {
        cfg = virtualChannelsBackend.channel_source_config(channelNumber)
        if (bookingMode)
            bookingTitles = virtualChannelsBackend.booking_list(channelNumber, bookingIndex,
                                                                bookingField)
        loading = true
        items = []
        status = "Loading…"
        virtualChannelsBackend.browse_source(channelNumber, kind, parentKey)
    }

    function isOn(item) {
        if (bookingMode) return bookingTitles.indexOf(item.label) >= 0
        if (isExclusionLevel) {
            var excl = (kind === "seasons") ? (cfg.excludedSeasons || [])
                                            : (cfg.excludedEpisodes || [])
            return excl.indexOf(item.id) < 0
        }
        var list = cfg[listField] || []
        return list.indexOf(item.label) >= 0
    }

    function toggle(item) {
        if (channelNumber < 0) return

        if (bookingMode) {
            var picked = bookingTitles.slice()
            var where = picked.indexOf(item.label)
            if (where >= 0) picked.splice(where, 1)
            else            picked.push(item.label)
            if (!virtualChannelsBackend.set_booking_list(channelNumber, bookingIndex,
                                                        bookingField, picked)) {
                status = "Could not save"
                return
            }
            bookingTitles = virtualChannelsBackend.booking_list(channelNumber, bookingIndex,
                                                               bookingField)
            status = ""
            return
        }

        if (isExclusionLevel) {
            var nowOn = isOn(item)
            if (!virtualChannelsBackend.set_channel_excluded(
                    channelNumber, kind, item.id, /*excluded*/ nowOn)) {
                status = "Could not save"
                return
            }
        } else {
            var list = (cfg[listField] || []).slice()
            var at = list.indexOf(item.label)
            if (at >= 0) list.splice(at, 1)
            else         list.push(item.label)
            if (!virtualChannelsBackend.set_channel_list(channelNumber, listField, list)) {
                status = "Could not save"
                return
            }
        }
        cfg = virtualChannelsBackend.channel_source_config(channelNumber)
        status = ""
    }

    function initialOf(label) {
        var t = (label || "").toUpperCase()
        t = t.replace(/^(THE |A |AN )/, "")
        return t.charAt(0)
    }

    function jumpPage(direction) {
        if (items.length === 0) return
        var next = itemList.currentIndex + direction * 10
        itemList.currentIndex = Math.max(0, Math.min(items.length - 1, next))
    }

    function jumpTo(letter) {
        if (items.length === 0) return false
        for (var i = 0; i < items.length; i++) {
            if (initialOf(items[i].label) === letter) {
                itemList.currentIndex = i
                return true
            }
        }
        return false
    }

    function jumpLetter(direction) {
        if (items.length === 0) return
        var i = itemList.currentIndex
        var here = initialOf(items[i].label)

        if (direction > 0) {
            while (i < items.length - 1) {
                i++
                if (initialOf(items[i].label) !== here) break
            }
        } else {
            while (i > 0 && initialOf(items[i - 1].label) === here) i--
            if (i > 0) {
                i--
                var prev = initialOf(items[i].label)
                while (i > 0 && initialOf(items[i - 1].label) === prev) i--
            }
        }
        itemList.currentIndex = i
    }

    function descend(item) {
        if (!canDescend) return
        navigateTo("modules/virtual_channels/views/SourceBrowser.qml", {
            moduleId:      browserRoot.moduleId,
            channelNumber: browserRoot.channelNumber,
            kind: browserRoot.kind === "shows" ? "seasons" : "episodes",
            parentKey: item.id,
            title: item.label
        }, { currentIndex: itemList.currentIndex })
    }

    Component.onCompleted: reload()

    Connections {
        target: virtualChannelsBackend
        function onSourceBrowseReady(kind, list) {
            if (kind !== browserRoot.kind) return
            browserRoot.loading = false
            browserRoot.items = list || []
            browserRoot.status = browserRoot.items.length === 0 ? "Nothing here" : ""
            if (browserRoot.restoreIndex >= 0 && browserRoot.items.length > 0) {
                itemList.currentIndex = Math.min(browserRoot.restoreIndex,
                                                 browserRoot.items.length - 1)
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
                browserRoot.restoreIndex = -1
            }
        }
        function onSourceBrowseFailed(kind, reason) {
            if (kind !== browserRoot.kind) return
            browserRoot.loading = false
            browserRoot.status = reason
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
            return
        }
        if (loading || items.length === 0) { event.accepted = true; return }

        if (event.key === Qt.Key_Up) {
            itemList.currentIndex = (itemList.currentIndex - 1 + items.length) % items.length
        } else if (event.key === Qt.Key_Down) {
            itemList.currentIndex = (itemList.currentIndex + 1) % items.length
        } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_ChannelUp) {
            jumpPage(-1)
        } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_ChannelDown) {
            jumpPage(1)
        } else if (event.key === Qt.Key_Left) {
            if (!canDescend) jumpLetter(-1)
        } else if (event.key === Qt.Key_Right) {
            if (canDescend) descend(items[itemList.currentIndex])
            else            jumpLetter(1)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            toggle(items[itemList.currentIndex])
        } else if ((event.key >= Qt.Key_A && event.key <= Qt.Key_Z)
                   || (event.key >= Qt.Key_0 && event.key <= Qt.Key_9)) {
            var letter = String.fromCharCode(event.key)
            status = jumpTo(letter) ? "" : "Nothing under " + letter
        }
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: root.surfaceColor }

    AppBar {
        id: appBar
        iconSource: browserRoot.moduleIcon
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.075
        anchors.leftMargin: root.sw * 0.125
        title: browserRoot.heading
    }

    Text {
        anchors.centerIn: parent
        visible: browserRoot.loading || browserRoot.items.length === 0
        text: browserRoot.loading ? "Loading…" : browserRoot.status
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0333333
    }

    ListView {
        id: itemList
        visible: !browserRoot.loading && browserRoot.items.length > 0
        anchors.top: appBar.bottom
        anchors.topMargin: root.sh * 0.025
        anchors.bottom: hint.top
        anchors.bottomMargin: root.sh * 0.02
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75
        clip: true
        model: browserRoot.items
        currentIndex: 0
        highlightMoveDuration: 0
        interactive: false

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.065

            required property int index
            required property var modelData
            readonly property bool selected: index === itemList.currentIndex
            readonly property bool on: browserRoot.isOn(modelData)

            Rectangle {
                anchors.fill: parent
                color: parent.selected ? root.accentColor : "transparent"
            }

            Rectangle {
                id: box
                width: root.sh * 0.030
                height: width
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0125
                color: parent.on ? (parent.selected ? root.surfaceColor : root.primaryColor)
                                 : "transparent"
                border.color: parent.selected ? root.surfaceColor : root.tertiaryColor
                border.width: 2
            }

            Text {
                text: modelData.label
                color: parent.selected ? root.surfaceColor
                                       : (parent.on ? root.primaryColor : root.tertiaryColor)
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: box.right
                anchors.leftMargin: root.sw * 0.0187
                anchors.right: subText.left
                anchors.rightMargin: root.sw * 0.0125
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.0354
            }

            Text {
                id: subText
                text: modelData.sub || (browserRoot.canDescend ? "►" : "")
                color: parent.selected ? root.surfaceColor : root.tertiaryColor
                font.family: root.globalFont
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.0125
                font.pixelSize: root.sh * 0.0292
            }
        }
    }

    Text {
        id: hint
        anchors.bottom: footer.top
        anchors.bottomMargin: root.sh * 0.0208
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75
        horizontalAlignment: Text.AlignHCenter
        text: browserRoot.bookingMode
              ? (browserRoot.bookingTitles.length === 0
                 ? "NOTHING TICKED HERE"
                 : "TICKED CAN AIR IN THIS SLOT")
              : browserRoot.isExclusionLevel
                ? "TICKED MEANS IT AIRS ON THIS CHANNEL"
                : "TICKED MEANS THIS CHANNEL DRAWS FROM IT"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0271
    }

    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":MOVE "
              + (browserRoot.canDescend ? root.hints.change + ":OPEN "
                                        : root.hints.change + ":LETTER  A-Z:JUMP ")
              + root.hints.select + ":TOGGLE"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.0833
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0292
    }
}
