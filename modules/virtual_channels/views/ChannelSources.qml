import QtQuick
import Components

FocusScope {
    id: sourcesRoot

    focus: true
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: navParams.navListState || ({})
    property string moduleId:      navParams.moduleId || ""
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var cfg: ({})
    property string status: ""
    property bool building: false

    readonly property var rows: {
        var r = ["source"]
        if (cfg.source === "local") {
            // Local files are a library like any other source: the two things a
            // media folder can hold, straight off this screen. There is no
            // intermediate list, and no offer to add from a server the channel
            // is not sourced from.
            // Films are not a pool: they are booked into Movie Slots, which is
            // the row below. A flat list of films would air them as ordinary
            // programmes and duplicate that mechanism.
            r.push("series")
        } else if (cfg.source !== undefined) {
            r.push("series")
            r.push("collections")
            if (cfg.supportsPlaylists) r.push("playlists")
        }
        // Idents hang off the shows already picked, so this only appears once
        // there is something to hang them on. It is not a second way to choose
        // what airs -- that is Series, for every source, always.
        if (sourcesRoot.programmeSources.length > 0) r.push("idents")
        r.push("slots")
        r.push("logo")
        r.push("order")
        r.push("timing")
        if (sourcesRoot.gridMinutes === 0) r.push("ads")
        r.push("breaks")
        r.push("rebuild")
        r.push("rename")
        r.push("delete")
        return r
    }
    readonly property int rowCount: rows.length
    readonly property var sources: cfg.available || ["local"]

    property int current: 0

    property int slotCount: 0
    property var interstitials: []
    property bool armedToDelete: false
    property var programmeSources: []
    property string channelLogo: ""
    property int gridMinutes: 0
    property int adsPerBreak: 0
    property string order: "sequential"

    readonly property var gridChoices: [0, 15, 30, 60]

    function reload() {
        cfg = virtualChannelsBackend.channel_source_config(channelNumber)
        slotCount = virtualChannelsBackend.channel_bookings(channelNumber).length
        interstitials = virtualChannelsBackend.channel_interstitials(channelNumber)
        programmeSources = virtualChannelsBackend.channel_pool(channelNumber, "programmes")
        channelLogo = virtualChannelsBackend.channel_logo(channelNumber) || ""
        var timing = virtualChannelsBackend.channel_timing(channelNumber)
        gridMinutes = timing.gridMinutes
        adsPerBreak = timing.adsPerBreak
        order       = timing.order
        if (current >= rowCount) current = rowCount - 1
    }

    function cycleSource(direction) {
        if (building || sources.length < 2) return
        var at = sources.indexOf(cfg.source)
        if (at < 0) at = 0
        var next = sources[(at + direction + sources.length) % sources.length]
        if (!virtualChannelsBackend.set_channel_source(channelNumber, next)) {
            status = "Could not change source"
            return
        }
        status = ""
        reload()
    }

    function countOf(field) {
        var l = cfg[field] || []
        return l.length
    }

    function interstitialCount() {
        var total = 0
        for (var i = 0; i < interstitials.length; i++) total += interstitials[i].count
        return total
    }

    function excludedCount() {
        return (cfg.excludedSeasons || []).length + (cfg.excludedEpisodes || []).length
    }

    function labelFor(i) {
        switch (rows[i]) {
        case "source":       return "Source"
        case "idents":       return "Show Idents"
        case "series":       return "Series"
        case "collections":  return "Collections"
        case "playlists":    return "Playlists"
        case "slots":        return "Movie Slots"
        case "logo":         return "Logo"
        case "order":        return "Order"
        case "timing":       return "Timing"
        case "ads":          return "Per Break"
        case "breaks":       return "Breaks"
        case "rebuild":      return building ? "Rebuilding…" : "Rebuild This Channel"
        case "rename":       return "Rename"
        case "delete":       return sourcesRoot.armedToDelete ? "Delete — Press Again" : "Delete Channel"
        }
        return ""
    }

    function valueFor(i) {
        switch (rows[i]) {
        case "source": return (cfg.sourceName || "Local Files").toUpperCase()
        case "idents": {
            var withIdents = 0
            for (var q = 0; q < sourcesRoot.programmeSources.length; q++) {
                var pe = sourcesRoot.programmeSources[q]
                if ((pe.intros && pe.intros.length) || (pe.outros && pe.outros.length)) withIdents++
            }
            return withIdents === 0 ? "NONE"
                                    : withIdents + " OF " + sourcesRoot.programmeSources.length
        }
        case "series": {
            var n = countOf("match")
            var ex = excludedCount()
            if (n === 0) return "NONE"
            return n + (ex > 0 ? " · " + ex + " OFF" : "")
        }
        case "collections": return countOf("collections") === 0 ? "NONE" : String(countOf("collections"))
        case "playlists":   return countOf("playlists")   === 0 ? "NONE" : String(countOf("playlists"))
        case "slots": {
            var n = sourcesRoot.slotCount
            return n === 0 ? "NONE" : (n === 1 ? "1 SLOT" : n + " SLOTS")
        }
        case "logo":
            return sourcesRoot.channelLogo === "" ? "DEFAULT"
                 : sourcesRoot.channelLogo.replace(/\.[^.]+$/, "").toUpperCase()
        case "breaks": {
            var n = sourcesRoot.interstitialCount()
            return n === 0 ? "NONE" : n + " CLIPS"
        }
        case "order":
            return sourcesRoot.order === "shuffle" ? "SHUFFLE" : "IN ORDER"
        case "timing":
            return sourcesRoot.gridMinutes === 0
                   ? "FREE RUN" : "ON THE " + sourcesRoot.gridMinutes + " MIN"
        case "ads":
            return sourcesRoot.adsPerBreak === 0 ? "NONE" : String(sourcesRoot.adsPerBreak)
        }
        return ""
    }

    function helpFor(i) {
        var server = cfg.sourceName || "the server"
        switch (rows[i]) {
        case "source":
            return sources.length < 2
                   ? "Only local files are set up. Sign in to Plex, Jellyfin or Emby to add more."
                   // "Server" is wrong for local files, which are the one source
                   // that is not one.
                   : (cfg.source === "local"
                      ? "Left and right to choose where this channel's shows come from. Changing it clears its picks."
                      : "Left and right to change server. Changing it clears this channel's picks.")
        // These three overlap, and which to use is not obvious, so each says what
        // it is FOR rather than only what it is. The distinction that matters:
        // series are picked show by show and can be narrowed; a collection or
        // playlist arrives whole and cannot.
        case "series":      return "Shows picked one by one. Open one to switch off seasons or episodes you don't want."
        case "collections": return "A group kept on " + server + ", added whole — everything in it airs, even shows not ticked in Series."
        case "playlists":   return "A list kept on " + server + ", added whole. Change it there and this channel follows on its next rebuild."
        case "idents":      return "A show can have its own intro or outro instead of the channel's. Anything not set here falls back to Breaks."
        case "slots":       return "Movies at fixed times, each drawing on its own set of movies."
        case "logo":        return "The mark this channel flies in the corner. Size and position are in Settings."
        case "order":       return sourcesRoot.order === "shuffle"
                                   ? "Series take turns, and everything plays once before anything repeats."
                                   : "Episodes play in order, which is what a series wants."
        case "timing":      return sourcesRoot.gridMinutes === 0
                                   ? "Free run: each program starts when the last one ended."
                                   : "Every program starts on the clock. Breaks fill the rest; the card holds any remainder."
        case "ads":         return "How many things play between programs. Free run only — on a clock the gap decides."
        case "breaks":      return "What plays between programs: station IDs, bumps, commercials and outros."
        case "rebuild":     return "Rebuild the schedule so source changes actually air."
        case "rename":      return "Change what this channel is called."
        case "delete":      return sourcesRoot.armedToDelete
                                   ? "Press again to remove this channel, or move away to keep it."
                                   : "Remove this channel. Its schedule goes with it."
        }
        return ""
    }

    function cycles(i) {
        var r = rows[i]
        return r === "source" || r === "order" || r === "timing" || r === "ads"
    }

    function step(delta) {
        if (building) return
        var r = rows[current]
        if (r === "source") { cycleSource(delta); return }
        if (r === "order") {
            var next = order === "shuffle" ? "sequential" : "shuffle"
            if (!virtualChannelsBackend.set_channel_order(channelNumber, next))
                status = "Could not change the order"
            else { status = ""; reload() }
            return
        }
        if (r === "timing") {
            var at = gridChoices.indexOf(gridMinutes)
            if (at < 0) at = 0
            var next = gridChoices[(at + delta + gridChoices.length) % gridChoices.length]
            if (!virtualChannelsBackend.set_channel_grid(channelNumber, next))
                status = "Could not change the timing"
            else { status = ""; reload() }
            return
        }
        if (r === "ads") {
            var n = Math.max(0, Math.min(4, adsPerBreak + delta))
            if (!virtualChannelsBackend.set_channel_ads(channelNumber, n))
                status = "Could not change the breaks"
            else { status = ""; reload() }
        }
    }

    function open(i) {
        if (building) return
        var row = rows[i]

        if (row === "rename") {
            appCore.save_setting(moduleId, "rename_buffer", "")
            navigateTo("modules/virtual_channels/views/TextEntry.qml", {
                moduleId: sourcesRoot.moduleId,
                settingKey: "rename_buffer",
                title: "Name Channel " + sourcesRoot.channelNumber,
                initialText: sourcesRoot.channelName
            }, { currentIndex: sourcesRoot.current, renameThis: true })
            return
        }
        if (row === "delete") {
            if (!armedToDelete) { armedToDelete = true; status = ""; return }
            armedToDelete = false
            if (virtualChannelsBackend.is_generating()) {
                status = "A channel is still building — try again in a moment"
                return
            }
            if (virtualChannelsBackend.delete_channel(channelNumber)) goBack()
            else status = "Could not remove this channel"
            return
        }

        if (row === "rebuild") {
            building = true
            status = "Rebuilding…"
            virtualChannelsBackend.regenerate(channelNumber)
            return
        }
        if (row === "source") { cycleSource(1); return }
        if (row === "logo") {
            navigateTo("modules/virtual_channels/views/LogoPicker.qml", {
                moduleId:      sourcesRoot.moduleId,
                channelNumber: sourcesRoot.channelNumber,
                title: sourcesRoot.channelName
            }, { currentIndex: sourcesRoot.current })
            return
        }
        if (row === "idents") {
            navigateTo("modules/virtual_channels/views/PoolEditor.qml", {
                moduleId:      sourcesRoot.moduleId,
                channelNumber: sourcesRoot.channelNumber,
                channelName:   sourcesRoot.channelName,
                pool:          "programmes",
                poolLabel:     "Show Idents",
                // Picking happens in Series now; this screen only attaches
                // intros and outros to what is already there.
                identsOnly:    true
            }, { currentIndex: sourcesRoot.current })
            return
        }
        if (row === "breaks") {
            navigateTo("modules/virtual_channels/views/Interstitials.qml", {
                moduleId:      sourcesRoot.moduleId,
                channelNumber: sourcesRoot.channelNumber,
                channelName:   sourcesRoot.channelName
            }, { currentIndex: sourcesRoot.current })
            return
        }
        if (row === "slots") {
            navigateTo("modules/virtual_channels/views/Bookings.qml", {
                moduleId:      sourcesRoot.moduleId,
                channelNumber: sourcesRoot.channelNumber,
                channelName:   sourcesRoot.channelName
            }, { currentIndex: sourcesRoot.current })
            return
        }

        navigateTo("modules/virtual_channels/views/SourceBrowser.qml", {
            moduleId:      sourcesRoot.moduleId,
            channelNumber: sourcesRoot.channelNumber,
            kind: row === "series" ? "shows" : row,
            title: sourcesRoot.channelName
        }, { currentIndex: sourcesRoot.current })
    }

    function applyPendingRename() {
        if (!navListState.renameThis) return
        var typed = appCore.get_setting(moduleId, "rename_buffer")
        if (typed && String(typed).trim() !== "") {
            if (virtualChannelsBackend.rename_channel(channelNumber, String(typed))) {
                channelName = String(typed).trim()
                status = "Renamed"
            } else {
                status = "Rename failed"
            }
        }
        appCore.save_setting(moduleId, "rename_buffer", "")
        navListState = {}
    }

    Component.onCompleted: {
        var restore = navListState.currentIndex
        applyPendingRename()
        reload()
        if (restore !== undefined) current = Math.min(restore, rowCount - 1)
    }

    Connections {
        target: virtualChannelsBackend
        function onGenerationProgress(ch, done, total) {
            if (ch !== sourcesRoot.channelNumber) return
            sourcesRoot.status = total > 0 ? "Building… " + done + " / " + total : "Building…"
        }
        function onGenerationFinished(ch, ok, message) {
            if (ch !== sourcesRoot.channelNumber) return
            sourcesRoot.building = false
            sourcesRoot.status = (ok ? "Rebuilt: " : "Failed: ") + message
            sourcesRoot.reload()
        }
    }

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: sourcesRoot.moduleIcon
        title: sourcesRoot.channelName + " — Settings"
        rows: sourcesRoot.rows
        current: sourcesRoot.current
        onCurrentChanged: { sourcesRoot.current = current; sourcesRoot.armedToDelete = false }
        status: sourcesRoot.status
        busy: sourcesRoot.building
        labelFor: function(i) { return sourcesRoot.labelFor(i) }
        valueFor: function(i) { return sourcesRoot.valueFor(i) }
        helpFor:  function(i) { return sourcesRoot.helpFor(i) }
        cycles:   function(i) { return sourcesRoot.cycles(i) }
        onStep:     function(d) { sourcesRoot.step(d) }
        onActivate: function(i) { sourcesRoot.open(i) }
        onBack:     function() { sourcesRoot.goBack() }
    }
}
