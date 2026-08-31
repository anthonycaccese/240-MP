import QtQuick
import Components

FocusScope {
    id: interRoot

    focus: true
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: navParams.navListState || ({})
    property string moduleId: navParams.moduleId || ""
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string status: ""
    property bool building: false

    readonly property var rows: ["intros", "bumps", "commercials", "outros", "rebuild"]
    readonly property int rowCount: rows.length
    property int current: 0

    function reload() { poolsChanged() }
    property int poolsRevision: 0
    function poolsChanged() { poolsRevision++ }

    function poolSummary(kind) {
        var _ = poolsRevision
        var list = virtualChannelsBackend.channel_pool(channelNumber, kind)
        if (list.length === 0) return "NONE"
        var counted = 0, uncounted = 0
        for (var i = 0; i < list.length; i++) {
            if (list[i].count >= 0) counted += list[i].count
            else uncounted++
        }
        // How many clips, not what the folder is called. The folder under Intros
        // is usually called "intro", so naming it here just said INTRO next to a
        // row already labelled INTROS, and buried the number that matters.
        if (uncounted === 0)
            return counted + (counted === 1 ? " CLIP" : " CLIPS")
        return list.length + (list.length === 1 ? " SOURCE" : " SOURCES")
    }

    function labelFor(i) {
        switch (rows[i]) {
        case "intros":      return "Intros"
        case "bumps":       return "Bumps"
        case "commercials": return "Commercials"
        case "outros":      return "Outros"
        case "rebuild":     return "Rebuild This Channel"
        }
        return ""
    }

    function valueFor(i) {
        if (rows[i] === "rebuild") return ""
        return poolSummary(rows[i])
    }

    function helpFor(i) {
        switch (rows[i]) {
        // Names the row that overrides these, so the two screens describe one
        // rule from both ends rather than each hinting at the other.
        case "intros":      return "Leads into a program, ending on the mark on a clock channel. Open to see which shows override it."
        case "bumps":       return "Short pieces between the commercials and the program either side of them. The same for every show."
        case "commercials": return "The commercials themselves. A clock channel packs as many as the gap allows. The same for every show."
        case "outros":      return "Played as a program ends, before the break. Open to see which shows override it."
        case "rebuild":     return "Rebuild the schedule so a change here actually airs."
        }
        return ""
    }

    function cycles(i) { return false }

    function open(i) {
        if (building) return
        var row = rows[i]

        if (row === "rebuild") {
            building = true
            status = "Rebuilding…"
            virtualChannelsBackend.regenerate(channelNumber)
            return
        }

        // Intros and outros are the two a single show can override, so they open
        // the default-and-exceptions list. Bumps and commercials have no such
        // thing and go straight to their pool.
        if (row === "intros" || row === "outros") {
            navigateTo("modules/virtual_channels/views/IdentLevels.qml", {
                moduleId:      interRoot.moduleId,
                channelNumber: interRoot.channelNumber,
                channelName:   interRoot.channelName,
                kind:          row
            }, { currentIndex: interRoot.current })
            return
        }

        navigateTo("modules/virtual_channels/views/PoolEditor.qml", {
            moduleId:      interRoot.moduleId,
            channelNumber: interRoot.channelNumber,
            channelName:   interRoot.channelName,
            pool:          row,
            poolLabel:     interRoot.labelFor(i)
        }, { currentIndex: interRoot.current })
    }

    Component.onCompleted: {
        reload()
        if (navListState.currentIndex !== undefined) current = navListState.currentIndex
    }

    Connections {
        target: virtualChannelsBackend
        function onGenerationProgress(ch, done, total) {
            if (ch !== interRoot.channelNumber) return
            interRoot.status = total > 0 ? "Building… " + done + " / " + total : "Building…"
        }
        function onGenerationFinished(ch, ok, message) {
            if (ch !== interRoot.channelNumber) return
            interRoot.building = false
            interRoot.status = (ok ? "Rebuilt: " : "Failed: ") + message
            interRoot.reload()
        }
    }

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: interRoot.moduleIcon
        title: interRoot.channelName !== "" ? "Breaks — " + interRoot.channelName : "Breaks"
        rows: interRoot.rows
        current: interRoot.current
        onCurrentChanged: interRoot.current = current
        status: interRoot.status
        busy: interRoot.building
        labelFor: function(i) { return interRoot.labelFor(i) }
        valueFor: function(i) { return interRoot.valueFor(i) }
        helpFor:  function(i) { return interRoot.helpFor(i) }
        cycles:   function(i) { return interRoot.cycles(i) }
        onActivate: function(i) { interRoot.open(i) }
        onBack:     function() { interRoot.goBack() }
    }
}
