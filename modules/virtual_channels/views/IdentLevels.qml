import QtQuick
import Components

// Which intro (or outro) a program gets: the default first, and every show
// that overrides it beneath. Nothing off this screen decides what plays.
FocusScope {
    id: levelsRoot

    focus: true
    property var navParams: ({})
    property string moduleId: navParams.moduleId || ""
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: navParams.navListState || ({})
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""
    // "intros" or "outros" -- the two pools a single program can override.
    property string kind:          navParams.kind          || "intros"

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string status: ""
    property var    shows: []
    property int    current: 0

    readonly property string kindWord: kind === "outros" ? "outro" : "intro"

    readonly property var rows: {
        var r = ["channel"]
        for (var i = 0; i < shows.length; i++) r.push("show:" + i)
        return r
    }
    readonly property int rowCount: rows.length

    function reload() {
        // Visibility changes on the way out too, when the backend is gone.
        if (!virtualChannelsBackend) return
        // Only programmes can carry their own, so only they are listed. A show
        // with nothing of its own still appears: seeing that it falls back is
        // the point of the screen.
        shows = virtualChannelsBackend.channel_pool(channelNumber, "programmes") || []
        if (current >= rows.length) current = Math.max(0, rows.length - 1)
    }

    function showAt(i) {
        var row = rows[i]
        if (row === undefined || row.indexOf("show:") !== 0) return null
        return shows[parseInt(row.substring(5))]
    }

    function ownFolders(entry) {
        if (!entry) return []
        var f = entry[levelsRoot.kind]
        return (f === undefined || f === null) ? [] : f
    }

    function channelClips() {
        // Runs from a value binding, which outlives the context property.
        if (!virtualChannelsBackend) return { sources: 0, clips: 0 }
        var list = virtualChannelsBackend.channel_pool(channelNumber, kind) || []
        var n = 0
        for (var i = 0; i < list.length; i++) if (list[i].count >= 0) n += list[i].count
        return { sources: list.length, clips: n }
    }

    function labelFor(i) {
        if (rows[i] === "channel") return "Every Show"
        var e = showAt(i)
        return e ? String(e.name) : ""
    }

    function valueFor(i) {
        if (rows[i] === "channel") {
            var c = channelClips()
            if (c.sources === 0) return "NONE"
            return c.clips + (c.clips === 1 ? " CLIP" : " CLIPS")
        }
        var own = ownFolders(showAt(i))
        return own.length === 0 ? "USES DEFAULT" : "OWN"
    }

    function helpFor(i) {
        if (rows[i] === "channel")
            return "The " + kindWord + " every show uses unless it has its own below."
        var e = showAt(i)
        if (!e) return ""
        return ownFolders(e).length === 0
               ? String(e.name) + " uses the default above."
               : String(e.name) + " has its own " + kindWord + ", so the default is not used for it."
    }

    function cycles(i) { return false }

    function open(i) {
        if (rows[i] === "channel") {
            navigateTo("modules/virtual_channels/views/PoolEditor.qml", {
                moduleId:      levelsRoot.moduleId,
                channelNumber: levelsRoot.channelNumber,
                channelName:   levelsRoot.channelName,
                pool:          levelsRoot.kind,
                poolLabel:     levelsRoot.kind === "outros" ? "Outros" : "Intros"
            }, { currentIndex: levelsRoot.current })
            return
        }
        var idx = parseInt(rows[i].substring(5))
        var e = shows[idx]
        if (!e) return
        if (ownFolders(e).length === 0) {
            appCore.save_setting(levelsRoot.moduleId, "pool_buffer", "")
            navigateTo("modules/virtual_channels/views/SourcePick.qml", {
                moduleId:   levelsRoot.moduleId,
                source:     "local",
                settingKey: "pool_buffer",
                purpose:    "folders",
                title:      (levelsRoot.kind === "outros" ? "Outro" : "Intro")
                            + " — " + String(e.name)
            }, { currentIndex: levelsRoot.current, pickFor: idx })
            return
        }
        navigateTo("modules/virtual_channels/views/SourceIdents.qml", {
            moduleId:      levelsRoot.moduleId,
            channelNumber: levelsRoot.channelNumber,
            channelName:   levelsRoot.channelName,
            pool:          "programmes",
            entryIndex:    idx,
            entryName:     String(e.name),
            // Opened from the intro list, this edits intros; from the outro
            // list, outros. Showing both would put the same setting on two
            // screens again.
            only:          levelsRoot.kind
        }, { currentIndex: levelsRoot.current })
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

        var list = virtualChannelsBackend.channel_pool(channelNumber, "programmes")
        if (which < 0 || which >= list.length) {
            status = "That show is no longer there"
            return
        }
        var next = {}
        for (var k in list[which])
            if (k !== "intros_count" && k !== "outros_count") next[k] = list[which][k]
        next[levelsRoot.kind] = [folder]
        list[which] = next
        if (!virtualChannelsBackend.set_channel_pool(channelNumber, "programmes", list)) {
            status = "Could not save that change"
            return
        }
        status = "Saved — rebuild to air the change"
    }

    Component.onCompleted: {
        // Read before applyPending, which clears the state it came in on.
        var restore = navListState.currentIndex
        reload()
        applyPending()
        reload()
        if (restore !== undefined) current = Math.min(restore, rows.length - 1)
    }

    onVisibleChanged: if (visible) reload()

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: levelsRoot.moduleIcon
        title: (levelsRoot.kind === "outros" ? "Outros" : "Intros")
               + (levelsRoot.channelName !== "" ? " — " + levelsRoot.channelName : "")
        rows: levelsRoot.rows
        current: levelsRoot.current
        onCurrentChanged: levelsRoot.current = current
        status: levelsRoot.status
        emptyText: "Nothing to show"
        labelFor: function(i) { return levelsRoot.labelFor(i) }
        valueFor: function(i) { return levelsRoot.valueFor(i) }
        helpFor:  function(i) { return levelsRoot.helpFor(i) }
        cycles:   function(i) { return levelsRoot.cycles(i) }
        onActivate: function(i) { levelsRoot.open(i) }
        onBack:     function() { levelsRoot.goBack() }
    }
}
