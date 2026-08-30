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

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    readonly property var serverKinds: [
        { kind: "collection", browse: "collections", label: "Collections" },
        { kind: "playlist",   browse: "playlists",   label: "Playlists" },
        { kind: "series",     browse: "shows",       label: "Series" }
    ]

    property string chosenKind: ""
    property var items: []
    property string status: ""
    property bool busy: false

    readonly property bool isLocal: source === "local"

    readonly property var rows: {
        if (isLocal) {
            var f = []
            for (var i = 0; i < items.length; i++) f.push("item:" + i)
            return f
        }
        if (chosenKind === "") {
            var k = []
            for (var j = 0; j < serverKinds.length; j++) k.push("kind:" + j)
            return k
        }
        var r = []
        for (var n = 0; n < items.length; n++) r.push("item:" + n)
        return r
    }
    property int current: 0

    function reload() {
        if (isLocal) items = virtualChannelsBackend.media_folders(3)
    }

    function labelFor(i) {
        var row = rows[i]
        if (row === undefined) return ""
        if (row.indexOf("kind:") === 0) return serverKinds[parseInt(row.substring(5))].label

        var e = items[parseInt(row.substring(5))]
        if (!e) return ""
        if (isLocal) {
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
        return isLocal ? String(e.count) : ""
    }

    function helpFor(i) {
        var row = rows[i]
        if (row === undefined) return ""
        if (row.indexOf("kind:") === 0) return "Ask this server for its " +
                                               serverKinds[parseInt(row.substring(5))].label.toLowerCase() + "."
        var e = items[parseInt(row.substring(5))]
        if (!e) return ""
        if (isLocal) return e.count === 0
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
            var k = serverKinds[parseInt(row.substring(5))]
            chosenKind = k.kind
            items = []
            current = 0
            busy = true
            status = "Asking " + source.toUpperCase() + "…"
            virtualChannelsBackend.browse_from(source, k.browse, "")
            return
        }

        var e = items[parseInt(row.substring(5))]
        if (!e) return
        if (isLocal) choose("folder", e.path)
        else         choose(chosenKind, e.label || e.id)
    }

    function back() {
        if (!isLocal && chosenKind !== "") {
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
        emptyText: pickRoot.isLocal ? "No folders under the media directory" : ""
        labelFor: function(i) { return pickRoot.labelFor(i) }
        valueFor: function(i) { return pickRoot.valueFor(i) }
        helpFor:  function(i) { return pickRoot.helpFor(i) }
        cycles:   function(i) { return pickRoot.cycles(i) }
        actionFor: function(i) { return pickRoot.actionFor(i) }
        onActivate: function(i) { pickRoot.open(i) }
        onBack:     function() { pickRoot.back() }
    }
}
