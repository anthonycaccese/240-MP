import QtQuick
import Components

FocusScope {
    id: pickRoot

    focus: true
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property string moduleId:   navParams.moduleId || ""
    property string source:     navParams.source     || "local"
    property string settingKey: navParams.settingKey || "pool_buffer"
    property string title:      navParams.title      || ""
    // What this picker is for: programmes come from a library of shows and
    // films, breaks from a folder of clips.
    property string purpose:    navParams.purpose    || "programmes"

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    // Only Plex serves playlists.
    readonly property var serverKinds: {
        var k = [{ kind: "collection", browse: "collections", label: "Collections" }]
        // The context property is gone once the Loader tears down.
        if (virtualChannelsBackend
            && virtualChannelsBackend.source_supports_playlists(pickRoot.source))
            k.push({ kind: "playlist", browse: "playlists", label: "Playlists" })
        k.push({ kind: "series", browse: "shows", label: "Series" })
        return k
    }

    // Every route in today sends local files to the folder browser below, so
    // this is a fallback -- but it has to stay right, or a local channel would
    // be offered a server's collections.
    readonly property var localKinds: [
        { kind: "series", browse: "shows",  label: "Series" },
        { kind: "movie",  browse: "movies", label: "Movies" }
    ]

    readonly property var kinds: isLocal ? localKinds : serverKinds

    property string chosenKind: ""
    property var items: []
    property string status: ""
    property bool busy: false

    readonly property bool isLocal: source === "local"
    // Only local break pools browse raw folders. Everything else browses a
    // library, local included.
    readonly property bool folderMode: isLocal && purpose === "folders"

    readonly property var rows: {
        if (folderMode) {
            var f = []
            for (var i = 0; i < items.length; i++) f.push("item:" + i)
            return f
        }
        if (chosenKind === "") {
            var k = []
            for (var j = 0; j < kinds.length; j++) k.push("kind:" + j)
            return k
        }
        var r = []
        for (var n = 0; n < items.length; n++) r.push("item:" + n)
        return r
    }
    property int current: 0

    function reload() {
        if (!folderMode) return
        // Break clips live under breaks/ and interstitials/. series/ and movies/
        // are programme material and have no business in this list.
        var all = virtualChannelsBackend.media_folders(3) || []
        var out = []
        for (var i = 0; i < all.length; i++) {
            var p = String(all[i].path || "")
            if (p.indexOf("series") === 0 || p.indexOf("movies") === 0) continue
            // A folder holding nothing usable is not a choice, it is a row in
            // the way. Parents are kept: their count includes what is beneath.
            if (Number(all[i].count) === 0) continue
            out.push(all[i])
        }
        items = out
    }

    function labelFor(i) {
        var row = rows[i]
        if (row === undefined) return ""
        if (row.indexOf("kind:") === 0) return kinds[parseInt(row.substring(5))].label

        var e = items[parseInt(row.substring(5))]
        if (!e) return ""
        if (folderMode) {
            var pad = ""
            for (var d = 0; d < e.depth; d++) pad += "   "
            return pad + e.name
        }
        return e.label || ""
    }

    function valueFor(i) {
        var row = rows[i]
        if (row === undefined || row.indexOf("kind:") === 0) return ""
        var e = items[parseInt(row.substring(5))]
        if (!e) return ""
        return folderMode ? String(e.count) : ""
    }

    function helpFor(i) {
        var row = rows[i]
        if (row === undefined) return ""
        if (row.indexOf("kind:") === 0) {
            var kl = kinds[parseInt(row.substring(5))].label.toLowerCase()
            return isLocal ? "Browse the " + kl + " in your media folder."
                           : "Ask this server for its " + kl + "."
        }
        var e = items[parseInt(row.substring(5))]
        if (!e) return ""
        if (folderMode) return e.count === 0
                            ? e.path + " — nothing usable in it"
                            : e.path + " — press to add it"
        return "Adds this to " + (pickRoot.title !== "" ? pickRoot.title.split(" — ")[0] : "this channel") + "."
    }

    function cycles(i) { return false }

    function actionFor(i) {
        var row = rows[i]
        if (row === undefined) return "OPEN"
        return row.indexOf("kind:") === 0 ? "OPEN" : "ADD"
    }

    function choose(kind, name) {
        appCore.save_setting(moduleId, settingKey, kind + "|" + name)
        goBack()
    }

    function open(i) {
        if (busy) return
        var row = rows[i]
        if (row === undefined) return

        if (row.indexOf("kind:") === 0) {
            var k = kinds[parseInt(row.substring(5))]
            chosenKind = k.kind
            items = []
            current = 0
            busy = true
            status = isLocal ? "Reading…" : "Asking " + source.toUpperCase() + "…"
            virtualChannelsBackend.browse_from(source, k.browse, "")
            return
        }

        var e = items[parseInt(row.substring(5))]
        if (!e) return
        if (folderMode) { choose("folder", e.path); return }
        // Local rows are stored by their id, which is the folder the library
        // found them in. The label carries a year for the viewer to read, and
        // matching on that would break the moment a folder was renamed.
        choose(chosenKind, isLocal ? (e.id || e.label) : (e.label || e.id))
    }

    function back() {
        if (!folderMode && chosenKind !== "") {
            chosenKind = ""
            items = []
            current = 0
            status = ""
            return
        }
        goBack()
    }

    Component.onCompleted: reload()

    Connections {
        target: virtualChannelsBackend
        function onSourceBrowseReady(kind, list) {
            pickRoot.busy = false
            pickRoot.items = list
            pickRoot.status = list.length === 0 ? "Nothing to show" : ""
        }
        function onSourceBrowseFailed(kind, reason) {
            pickRoot.busy = false
            pickRoot.items = []
            pickRoot.status = reason
        }
    }

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: pickRoot.moduleIcon
        title: pickRoot.title !== "" ? pickRoot.title : "Add A Source"
        rows: pickRoot.rows
        current: pickRoot.current
        onCurrentChanged: pickRoot.current = current
        status: pickRoot.status
        busy: pickRoot.busy
        emptyText: pickRoot.folderMode ? "No folders under the media directory" : ""
        labelFor: function(i) { return pickRoot.labelFor(i) }
        valueFor: function(i) { return pickRoot.valueFor(i) }
        helpFor:  function(i) { return pickRoot.helpFor(i) }
        cycles:   function(i) { return pickRoot.cycles(i) }
        actionFor: function(i) { return pickRoot.actionFor(i) }
        onActivate: function(i) { pickRoot.open(i) }
        onBack:     function() { pickRoot.back() }
    }
}
