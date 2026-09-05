import QtQuick
import Components

FocusScope {
    id: identRoot

    focus: true
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: navParams.navListState || ({})
    property string moduleId:      navParams.moduleId || ""
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""
    property string pool:          navParams.pool          || "programmes"
    property int    entryIndex:    navParams.entryIndex !== undefined ? navParams.entryIndex : -1
    property string entryName:     navParams.entryName     || ""
    // Which pool this screen edits. Set by the list it was opened from, so a
    // setting never appears on two screens at once.
    property string kind:          navParams.kind          || "intros"
    readonly property string kindWord: kind === "outros" ? "outro" : "intro"

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var entry: ({})
    property string status: ""

    readonly property bool hasOwnIdents: foldersOf(kind).length > 0
    readonly property var rows: hasOwnIdents ? [kind, "clear"] : [kind]
    property int current: 0

    property bool armedToClear: false

    function reload() {
        var list = virtualChannelsBackend.channel_pool(channelNumber, pool)
        entry = (entryIndex >= 0 && entryIndex < list.length) ? list[entryIndex] : ({})
    }

    function foldersOf(key) {
        var v = entry ? entry[key] : undefined
        return (v === undefined || v === null) ? [] : v
    }

    function labelFor(i) {
        switch (rows[i]) {
        case "intros": return "Intro"
        case "outros": return "Outro"
        case "clear":  return "Use The Channel's"
        }
        return ""
    }

    function countOf(key) {
        var n = entry ? entry[key + "_count"] : undefined
        return (n === undefined || n === null) ? -1 : n
    }

    function valueFor(i) {
        if (rows[i] === "clear") return armedToClear ? "CLEAR?" : ""
        var f = foldersOf(rows[i])
        if (f.length === 0) return "CHANNEL'S"
        var parts = String(f[0]).split("/").filter(function (p) { return p !== "" })
        var leaf = parts.length ? parts[parts.length - 1] : ""
        var generic = ["intro", "intros", "outro", "outros", "idents", "bumps"]
        var useful = (generic.indexOf(leaf.toLowerCase()) >= 0 && parts.length > 1)
                       ? parts[parts.length - 2] : leaf
        var n = countOf(rows[i])
        return useful.toUpperCase() + (n >= 0 ? " (" + n + ")" : "")
    }

    function helpFor(i) {
        if (rows[i] !== "clear" && foldersOf(rows[i]).length > 0 && countOf(rows[i]) === 0)
            return "That folder holds no clips, so the channel's own idents play instead."
        switch (rows[i]) {
        case "intros": return "A folder of idents to announce " + shortName()
                              + ", instead of the channel's."
        case "outros": return "A folder of idents to close " + shortName()
                              + ", instead of the channel's."
        case "clear":  return armedToClear
                              ? "Press again to forget it, or move away to keep it."
                              : "Forget it and let the channel's " + identRoot.kindWord
                                + " announce " + identRoot.shortName() + " again."
        }
        return ""
    }

    function shortName() {
        var n = String(entryName)
        return n === "" ? "this source" : (n.length > 22 ? n.substring(0, 22) + "…" : n)
    }

    function cycles(i) { return false }
    function actionFor(i) { return rows[i] === "clear" ? "CLEAR" : "CHOOSE" }

    function clampCurrent() {
        if (current > rows.length - 1) current = rows.length - 1
        if (current < 0) current = 0
    }

    function save(next) {
        var list = virtualChannelsBackend.channel_pool(channelNumber, pool)
        if (entryIndex < 0 || entryIndex >= list.length) {
            status = "That source is no longer there"
            return
        }
        list[entryIndex] = next
        if (!virtualChannelsBackend.set_channel_pool(channelNumber, pool, list)) {
            status = "Could not save that change"
            return
        }
        status = "Saved — rebuild to air the change"
        reload()
        clampCurrent()
    }

    function open(i) {
        var row = rows[i]
        if (row === "clear") {
            if (!armedToClear) { armedToClear = true; return }
            armedToClear = false
            var next = {}
            for (var k in entry)
                if (k !== identRoot.kind && k !== "intros_count" && k !== "outros_count")
                    next[k] = entry[k]
            save(next)
            return
        }
        armedToClear = false
        appCore.save_setting(moduleId, "pool_buffer", "")
        navigateTo("modules/virtual_channels/views/SourcePick.qml", {
            moduleId:   identRoot.moduleId,
            source:     "local",
            settingKey: "pool_buffer",
            purpose:    "folders",
            title:      labelFor(i) + " — " + shortName()
        }, { currentIndex: identRoot.current, pickFor: row })
    }

    function applyPending() {
        var which = navListState.pickFor
        if (which === undefined) return
        var picked = appCore.get_setting(moduleId, "pool_buffer")
        var raw = (picked === undefined || picked === null) ? "" : String(picked)
        appCore.save_setting(moduleId, "pool_buffer", "")
        navListState = {}
        if (raw === "") return

        var at = raw.indexOf("|")
        if (at <= 0) return
        var folder = raw.substring(at + 1)
        if (folder === "") return

        var next = {}
        for (var k in entry)
            if (k !== "intros_count" && k !== "outros_count") next[k] = entry[k]
        next[which] = [folder]
        save(next)
    }

    Component.onCompleted: {
        var restore = navListState.currentIndex
        reload()
        applyPending()
        if (restore !== undefined) current = Math.min(restore, rows.length - 1)
    }

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: identRoot.moduleIcon
        title: (identRoot.kind === "outros" ? "Outro" : "Intro")
               + " — " + identRoot.shortName()
        rows: identRoot.rows
        current: identRoot.current
        onCurrentChanged: { identRoot.current = current; identRoot.armedToClear = false }
        status: identRoot.status
        labelFor: function(i) { return identRoot.labelFor(i) }
        valueFor: function(i) { return identRoot.valueFor(i) }
        helpFor:  function(i) { return identRoot.helpFor(i) }
        cycles:   function(i) { return identRoot.cycles(i) }
        actionFor: function(i) { return identRoot.actionFor(i) }
        onActivate: function(i) { identRoot.open(i) }
        onBack:     function() { identRoot.goBack() }
    }
}
