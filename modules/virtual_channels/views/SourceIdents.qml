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
    // Empty means both. Set to "intros" or "outros" when reached from the list
    // for that one, so a setting does not appear on two screens at once.
    property string only:          navParams.only          || ""
    readonly property string onlyWord: only === "outros" ? "outro" : "intro"

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var entry: ({})
    property string status: ""

    // What clearing would actually forget. Scoped to one kind, that is only
    // that kind: offering to clear from the Intros screen a show whose only
    // override is an outro would take away something this screen never showed.
    readonly property bool hasOwnIdents: only !== ""
                                   ? foldersOf(only).length > 0
                                   : (foldersOf("intros").length > 0
                                      || foldersOf("outros").length > 0)
    readonly property var rows: only !== ""
                                ? (hasOwnIdents ? [only, "clear"] : [only])
                                : hasOwnIdents ? ["intros", "outros", "clear"]
                                       : ["intros", "outros"]
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
        case "clear":  return identRoot.only !== ""
                              ? (armedToClear
                                 ? "Press again to forget it, or move away to keep it."
                                 : "Forget it and let the channel's " + identRoot.onlyWord
                                   + " announce " + identRoot.shortName() + " again.")
                              : (armedToClear
                                 ? "Press again to forget both, or move away to keep them."
                                 : "Forget both and let the channel's own idents announce it again.")
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
            // Only what this screen showed. Reached from the Intros list it
            // used to drop the show's outro as well -- a setting made on a
            // different screen, gone with no mention of it anywhere here.
            var drop = identRoot.only !== ""
                       ? [identRoot.only, identRoot.only + "_count"]
                       : ["intros", "outros", "intros_count", "outros_count"]
            var next = {}
            for (var k in entry)
                if (drop.indexOf(k) < 0 && k !== "intros_count" && k !== "outros_count")
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
        // Named for what it edits. Reached from the Intros list it shows
        // only the intro, and calling that screen "Idents" made the same
        // job go by two names depending on how it was opened.
        title: (identRoot.only === "" ? "Idents"
                : identRoot.only === "outros" ? "Outro" : "Intro")
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
