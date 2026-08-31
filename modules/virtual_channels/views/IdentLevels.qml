import QtQuick
import Components

// Which intro (or outro) a program gets, shown as the thing it is: a default
// with exceptions under it.
//
// This used to be two screens describing one rule from opposite ends -- Breaks
// said "a show with its own uses that instead", Show Idents said "shows without
// one use the channel's" -- and a viewer had to hold both in their head to know
// what would actually air. Here the default is the first row and every show
// that overrides it is listed beneath, so the rule is visible rather than
// explained. Nothing decides what plays except what is on this screen.
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

    Component.onCompleted: {
        reload()
        if (navListState.currentIndex !== undefined) current = navListState.currentIndex
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
