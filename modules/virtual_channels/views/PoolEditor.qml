import QtQuick
import Components

FocusScope {
    id: poolRoot

    focus: true
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: navParams.navListState || ({})
    property string moduleId:      navParams.moduleId || ""
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""
    property string pool:          navParams.pool          || "programmes"
    property string poolLabel:     navParams.poolLabel     || "Sources"
    // Reached from Show Idents: the list is there to be given idents, not to be
    // added to. Choosing what airs belongs on one screen only.

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var entries: []
    property var sources: []
    property string status: ""
    property bool building: false
    property int armedToRemove: -1

    readonly property var rows: {
        var r = []
        for (var i = 0; i < entries.length; i++) r.push("entry:" + i)
        for (var s = 0; s < sources.length; s++) r.push("add:" + sources[s])
        r.push("rebuild")
        return r
    }
    property int current: 0

    function reload() {
        entries = virtualChannelsBackend.channel_pool(channelNumber, pool)
        sources = virtualChannelsBackend.available_sources()
        if (current >= rows.length) current = Math.max(0, rows.length - 1)
    }

    function entryAt(i) {
        var row = rows[i]
        if (row === undefined || row.indexOf("entry:") !== 0) return null
        return entries[parseInt(row.substring(6))]
    }

    function sourceLabel(s) {
        switch (s) {
        case "local":    return "Local Files"
        case "plex":     return "Plex"
        case "jellyfin": return "Jellyfin"
        case "emby":     return "Emby"
        }
        return s
    }

    // A folder row shows its path, not its last segment: under Bumps the segment
    // is "bump", which said nothing the title had not already said.
    function labelFor(i) {
        var row = rows[i]
        if (row === undefined) return ""
        if (row === "rebuild") return building ? "Rebuilding…" : "Rebuild This Channel"
        if (row.indexOf("add:") === 0) return "Add From " + sourceLabel(row.substring(4))

        var e = entryAt(i)
        if (!e) return ""
        if (e.kind === "folder") {
            var p = String(e.name)
            // The whole path, because the last segment alone is usually the
            // pool's own name -- "bump" under Bumps -- which distinguishes
            // nothing when a channel draws bumps from several folders.
            return p === "." ? "Media Root" : p
        }
        return e.name
    }

    // What a show has hanging off it, in the two words a column can hold.
    function valueFor(i) {
        var row = rows[i]
        if (row === undefined || row === "rebuild") return ""
        if (row.indexOf("add:") === 0) return ""

        var e = entryAt(i)
        if (!e) return ""
        if (parseInt(row.substring(6)) === armedToRemove) return "REMOVE?"
        var where = sourceLabel(e.src).toUpperCase()
        if (e.count >= 0) return where + " (" + e.count + ")"
        return where + " · " + String(e.kind).toUpperCase()
    }

    function helpFor(i) {
        var row = rows[i]
        if (row === undefined) return ""
        if (row === "rebuild") return "Rebuild the schedule so a change here actually airs."
        if (row.indexOf("add:") === 0)
            return row.substring(4) === "local"
                   ? "Choose a folder of clips under the media directory."
                   // Named to match what the picker will offer: playlists are
                   // Plex only.
                   : "Choose a " + (virtualChannelsBackend
                                    && virtualChannelsBackend.source_supports_playlists(row.substring(4))
                                    ? "collection, playlist or series"
                                    : "collection or series")
                     + " from " + sourceLabel(row.substring(4)) + "."

        var e = entryAt(i)
        if (!e) return ""
        if (parseInt(row.substring(6)) === armedToRemove) return "Press again to remove it, or move away to keep it."
        if (e.kind === "folder")
            return e.count === 0
                   ? e.name + " — no clips in it. " + root.hints.select + " removes it."
                   : e.name + " — " + e.count + " clip" + (e.count === 1 ? "" : "s")
                     + ". " + root.hints.select + " removes it."
        return String(e.kind) + " on " + sourceLabel(e.src) + ". "
               + root.hints.select + " removes it."
    }

    function cycles(i) { return false }

    function actionFor(i) {
        var row = rows[i]
        if (row === undefined) return "OPEN"
        return row.indexOf("entry:") === 0 ? "REMOVE" : "OPEN"
    }

    function save(list) {
        if (!virtualChannelsBackend.set_channel_pool(channelNumber, pool, list)) {
            status = "Could not save that change"
            return false
        }
        status = list.length === 0 ? "Empty — rebuild to air the change"
                                   : list.length + " source(s) — rebuild to air the change"
        reload()
        return true
    }

    function open(i) {
        if (building) return
        var row = rows[i]
        if (row === undefined) return

        if (row === "rebuild") {
            building = true
            status = "Rebuilding…"
            virtualChannelsBackend.regenerate(channelNumber)
            return
        }

        if (row.indexOf("add:") === 0) {
            appCore.save_setting(moduleId, "pool_buffer", "")
            navigateTo("modules/virtual_channels/views/SourcePick.qml", {
                moduleId: poolRoot.moduleId,
                source: row.substring(4),
                settingKey: "pool_buffer",
                // Programmes are chosen from a library; breaks are chosen from
                // a folder of clips. Saying which stops the picker offering a
                // folder of bumps as though it were a show.
                purpose: poolRoot.pool === "programmes" ? "programmes" : "folders",
                title: poolLabel + " — " + sourceLabel(row.substring(4))
            }, { currentIndex: poolRoot.current, addFrom: row.substring(4) })
            return
        }

        var idx = parseInt(row.substring(6))
        if (armedToRemove !== idx) {
            armedToRemove = idx
            status = ""
            return
        }
        armedToRemove = -1
        var next = []
        for (var k = 0; k < entries.length; k++) if (k !== idx) next.push(entries[k])
        save(next)
    }

    function applyPending() {
        var from = navListState.addFrom
        if (from === undefined) return
        var picked = appCore.get_setting(moduleId, "pool_buffer")
        var raw = (picked === undefined || picked === null) ? "" : String(picked)
        appCore.save_setting(moduleId, "pool_buffer", "")
        navListState = {}
        if (raw === "") return

        var at = raw.indexOf("|")
        if (at <= 0) return
        var kind = raw.substring(0, at)
        var name = raw.substring(at + 1)
        if (name === "") return

        var next = []
        for (var k = 0; k < entries.length; k++) next.push(entries[k])
        for (var j = 0; j < next.length; j++)
            if (next[j].src === from && String(next[j].name) === name
                && String(next[j].kind) === kind) {
                status = "Already in this pool"
                return
            }
        next.push({ src: from, kind: kind, name: name })
        save(next)
    }

    Component.onCompleted: {
        var restore = navListState.currentIndex
        reload()
        applyPending()
        if (restore !== undefined) current = Math.min(restore, Math.max(0, rows.length - 1))
    }

    Connections {
        target: virtualChannelsBackend
        function onGenerationProgress(ch, done, total) {
            if (ch !== poolRoot.channelNumber) return
            poolRoot.status = total > 0 ? "Building… " + done + " / " + total : "Building…"
        }
        function onGenerationFinished(ch, ok, message) {
            if (ch !== poolRoot.channelNumber) return
            poolRoot.building = false
            poolRoot.status = (ok ? "Rebuilt: " : "Failed: ") + message
            poolRoot.reload()
        }
    }

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: poolRoot.moduleIcon
        title: poolRoot.channelName !== "" ? poolRoot.poolLabel + " — " + poolRoot.channelName
                                           : poolRoot.poolLabel
        rows: poolRoot.rows
        current: poolRoot.current
        onCurrentChanged: { poolRoot.current = current; poolRoot.armedToRemove = -1 }
        status: poolRoot.status
        busy: poolRoot.building
        labelFor: function(i) { return poolRoot.labelFor(i) }
        valueFor: function(i) { return poolRoot.valueFor(i) }
        helpFor:  function(i) { return poolRoot.helpFor(i) }
        cycles:   function(i) { return poolRoot.cycles(i) }
        actionFor: function(i) { return poolRoot.actionFor(i) }
        onActivate:  function(i) { poolRoot.open(i) }
        onBack:      function() { poolRoot.goBack() }
    }
}
