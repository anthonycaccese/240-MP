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
    // The series these seasons or episodes belong to, carried down so every level
    // can answer the same question: does this channel draw from that series?
    property string seriesLabel:   navParams.seriesLabel  || ""
    // The other seasons of that series, handed to the episode level because
    // narrowing a series to one episode means switching the rest of them off and
    // only the season list knows what they are.
    property var    siblingSeasons: navParams.siblingSeasons || []
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
    // A season only airs if the channel draws from the series it belongs to, so
    // that is half of what its tick means. The other half is the exclusion list.
    readonly property string seriesName: seriesLabel !== "" ? seriesLabel
                                        : (kind === "seasons" ? heading : "")
    readonly property bool seriesSelected:
        seriesName !== "" && (cfg.match || []).indexOf(seriesName) >= 0
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
            if (!seriesSelected) return false
            if (kind === "episodes"
                && (cfg.excludedSeasons || []).indexOf(parentKey) >= 0) return false
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

        if (kind === "seasons") {
            if (!toggleSeason(item)) return
        } else if (kind === "episodes") {
            if (!toggleEpisode(item)) return
        } else if (isExclusionLevel) {
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

    // A tick anywhere means the same thing -- this airs -- so a tick low down
    // carries upward until that is true. Ticking a show puts the whole show on;
    // ticking a season puts that season on and the show with it; ticking one
    // episode, when nothing above it is on, puts that episode on and narrows the
    // show to it. Turning off the last thing airing at a level turns that level
    // off too, rather than leaving a show in the list with nothing to play.
    function selectSeries(list) {
        if (list.indexOf(seriesName) < 0) list.push(seriesName)
        if (virtualChannelsBackend.set_channel_list(channelNumber, "match", list)) return true
        status = "Could not save"
        return false
    }

    function dropSeries(list) {
        const at = list.indexOf(seriesName)
        if (at >= 0) list.splice(at, 1)
        virtualChannelsBackend.set_channel_list(channelNumber, "match", list)
    }

    function toggleSeason(item) {
        const wasAiring = isOn(item)
        const seriesList = (cfg.match || []).slice()

        if (!wasAiring) {
            // Starting from nothing narrows the series to this season. If the
            // series is already on, the other seasons are somebody's choice and
            // stay as they are.
            const narrowing = !seriesSelected
            if (narrowing && !selectSeries(seriesList)) return false
            if (narrowing) {
                for (var i = 0; i < items.length; i++)
                    virtualChannelsBackend.set_channel_excluded(
                        channelNumber, "seasons", items[i].id, items[i].id !== item.id)
            } else {
                virtualChannelsBackend.set_channel_excluded(
                    channelNumber, "seasons", item.id, false)
            }
            // every episode of it airs again, whatever was picked out before
            virtualChannelsBackend.clear_episode_exclusions(channelNumber, item.id)
            return true
        }

        if (!virtualChannelsBackend.set_channel_excluded(
                channelNumber, "seasons", item.id, /*excluded*/ true)) {
            status = "Could not save"
            return false
        }
        virtualChannelsBackend.clear_episode_exclusions(channelNumber, item.id)

        const excl = (virtualChannelsBackend.channel_source_config(channelNumber).excludedSeasons
                      || [])
        for (var j = 0; j < items.length; j++)
            if (excl.indexOf(items[j].id) < 0) return true      // something still airs

        dropSeries(seriesList)
        for (var k = 0; k < items.length; k++)
            virtualChannelsBackend.set_channel_excluded(channelNumber, "seasons", items[k].id, false)
        return true
    }

    function toggleEpisode(item) {
        const wasAiring = isOn(item)
        const seriesList = (cfg.match || []).slice()
        const seasonOff = (cfg.excludedSeasons || []).indexOf(parentKey) >= 0

        if (!wasAiring) {
            // Off only because it was picked out of a season that is otherwise
            // airing: put it back and leave everything else alone.
            if (seriesSelected && !seasonOff)
                return virtualChannelsBackend.set_channel_excluded(
                           channelNumber, "episodes", item.id, false, parentKey)

            // Otherwise nothing above it is on. Starting from nothing narrows the
            // series to this episode; if the series is already on and only this
            // season was off, that season comes back on narrowed to it and the
            // rest of the series is left alone.
            const narrowing = !seriesSelected
            if (narrowing && !selectSeries(seriesList)) return false
            if (narrowing) {
                for (var i = 0; i < siblingSeasons.length; i++)
                    virtualChannelsBackend.set_channel_excluded(
                        channelNumber, "seasons", siblingSeasons[i],
                        siblingSeasons[i] !== parentKey)
            } else {
                virtualChannelsBackend.set_channel_excluded(
                    channelNumber, "seasons", parentKey, false)
            }
            virtualChannelsBackend.clear_episode_exclusions(channelNumber, parentKey)
            for (var j = 0; j < items.length; j++)
                if (items[j].id !== item.id)
                    virtualChannelsBackend.set_channel_excluded(
                        channelNumber, "episodes", items[j].id, true, parentKey)
            return true
        }

        if (!virtualChannelsBackend.set_channel_excluded(
                channelNumber, "episodes", item.id, true, parentKey)) {
            status = "Could not save"
            return false
        }

        const epExcl = (virtualChannelsBackend.channel_source_config(channelNumber).excludedEpisodes
                        || [])
        for (var k = 0; k < items.length; k++)
            if (epExcl.indexOf(items[k].id) < 0) return true    // something still airs

        // The last episode of the season went off, so the season did too.
        virtualChannelsBackend.clear_episode_exclusions(channelNumber, parentKey)
        virtualChannelsBackend.set_channel_excluded(channelNumber, "seasons", parentKey, true)

        const seasonExcl = (virtualChannelsBackend.channel_source_config(channelNumber)
                            .excludedSeasons || [])
        for (var m = 0; m < siblingSeasons.length; m++)
            if (seasonExcl.indexOf(siblingSeasons[m]) < 0) return true

        dropSeries(seriesList)
        for (var n = 0; n < siblingSeasons.length; n++)
            virtualChannelsBackend.set_channel_excluded(channelNumber, "seasons",
                                                        siblingSeasons[n], false)
        return true
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
            title: item.label,
            seriesLabel: browserRoot.kind === "shows" ? item.label : browserRoot.seriesName,
            // Only the season list holds seasons; from the show list these are
            // shows, and passing them as seasons is how 246 of them once ended
            // up in a channel's excluded-seasons list.
            siblingSeasons: browserRoot.kind === "seasons"
                            ? browserRoot.items.map(function (s) { return s.id })
                            : []
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
