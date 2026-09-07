import QtQuick
import Components

// One block of a day plan: what it draws on, and for how long.
//
// There is no name row. A block's source is its name — naming them as well
// turns a day into a set of sub-channels, which is more machinery than saying
// "Samurai Jack for two hours" needs.
FocusScope {
    id: editRoot
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""

    property string moduleId:      navParams.moduleId || ""
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""
    property int    planIndex:     navParams.planIndex  !== undefined ? navParams.planIndex  : 0
    property int    blockIndex:    navParams.blockIndex !== undefined ? navParams.blockIndex : 0
    property var    navListState:  navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var plans: []
    property string status: ""
    property int current: 0
    property bool armedToDelete: false

    readonly property var plan:  plans.length > planIndex ? plans[planIndex] : null
    readonly property var block: (plan && (plan.blocks || []).length > blockIndex)
                                 ? plan.blocks[blockIndex] : null

    // The step a length moves by. The plan's grid is the unit everything in it
    // is measured in, so one press is one slot and there is no arithmetic.
    readonly property int step: plan && plan.gridMinutes > 0 ? plan.gridMinutes : 30

    // What this channel's source can actually be asked for. A folder of files
    // has no collections and no genres, so offering them here would open a
    // picker with nothing in it and leave a block that airs nothing.
    property var cfg: ({})
    // Plex calls a film's genres its categories, the same as every other screen
    // that offers them.
    readonly property string genreWordOne: cfg.source === "plex" ? "Category" : "Genre"

    readonly property var types: {
        if (cfg.source === "local") return ["series", "movie", "random"]
        return ["series", "collection", "genre", "movie", "random"]
    }

    // A random block draws on everything the channel gathered, so it has
    // nothing of its own to pick.
    readonly property bool picksASource: block && block.type !== "random"

    readonly property var rows: {
        var r = ["type"]
        if (picksASource) r.push("source")
        r.push("length")
        // A block that names one film has one thing to play, so there is no
        // order to choose. Everything else has a run to go through.
        if (!(block && block.type === "movie" && String(block.name || "") !== ""))
            r.push("order")
        // A block is played in and out as itself, or falls back to the
        // channel's. This is the only place that override is set, because the
        // block is the only thing on a planned channel that names a show.
        r.push("intros")
        r.push("outros")
        r.push("delete")
        return r
    }
    readonly property int rowCount: rows.length

    focus: true

    function reload() {
        cfg   = virtualChannelsBackend.channel_source_config(channelNumber)
        plans = virtualChannelsBackend.channel_plans(channelNumber)
        if (current >= rowCount) current = rowCount - 1
        if (current < 0) current = 0
    }

    function typeLabel(t) {
        if (t === "collection") return "COLLECTION"
        if (t === "genre")      return genreWordOne.toUpperCase()
        if (t === "movie")      return "MOVIE"
        if (t === "random")     return "RANDOM"
        return "SERIES"
    }

    function sourceLabel() {
        if (!block) return ""
        if (block.type === "movie")  return block.name !== "" ? block.name.toUpperCase() : "ANY MOVIE"
        if (block.name === "")       return "NO CONTENT"
        return block.name.toUpperCase()
    }

    function clockLabel(mins) {
        var h = Math.floor(mins / 60)
        var m = mins % 60
        var suffix = h < 12 ? "AM" : "PM"
        var hh = h % 12
        if (hh === 0) hh = 12
        return hh + ":" + (m < 10 ? "0" + m : m) + " " + suffix
    }

    function lengthLabel(mins) {
        var h = Math.floor(mins / 60)
        var m = mins % 60
        if (h > 0 && m > 0) return h + "H " + (m < 10 ? "0" + m : m) + "M"
        if (h > 0)          return h + "H"
        return m + "M"
    }

    // How much of the series this block is not airing. Counted rather than
    // named: the row has no room for a list, and none is a whole series.
    function offCount() {
        if (!block || !block.exclude) return 0
        var n = (block.exclude.seasons || []).length
        var bySeason = block.exclude.episodes || ({})
        for (var k in bySeason) n += (bySeason[k] || []).length
        return n
    }

    function foldersOf(key) {
        if (!block) return []
        var v = block[key]
        return (v === undefined || v === null) ? [] : v
    }

    function labelFor(i) {
        switch (rows[i]) {
        case "type":   return "Type"
        case "source": return block ? typeLabel(block.type).charAt(0)
                                      + typeLabel(block.type).slice(1).toLowerCase() : "Source"
        case "length": return "Length"
        case "order":  return "Order"
        case "intros": return "Intro"
        case "outros": return "Outro"
        case "delete": return armedToDelete ? "Press Again To Delete" : "Delete This Block"
        }
        return ""
    }

    // A block with none of its own plays the channel's, which is what the row
    // says rather than leaving it blank and letting it read as silence. Where
    // it has one, the folder is named: "1 folder" says a thing is set without
    // saying which, and the name is what the viewer chose it by.
    function identLabel(key) {
        var f = foldersOf(key)
        if (f.length === 0) return "CHANNEL'S"
        if (f.length > 1)   return f.length + " FOLDERS"
        var parts = String(f[0]).split("/").filter(function (p) { return p !== "" })
        var leaf = parts.length ? parts[parts.length - 1] : ""
        var generic = ["intro", "intros", "outro", "outros", "idents", "bumps"]
        var useful = (generic.indexOf(leaf.toLowerCase()) >= 0 && parts.length > 1)
                       ? parts[parts.length - 2] : leaf
        return useful.toUpperCase()
    }

    function valueFor(i) {
        switch (rows[i]) {
        case "type":   return block ? typeLabel(block.type) : ""
        case "source": return sourceLabel()
        case "length": return block ? lengthLabel(block.minutes) : ""
        case "order":  return shuffled() ? "SHUFFLED" : "IN ORDER"
        case "intros": return identLabel("intros")
        case "outros": return identLabel("outros")
        }
        return ""
    }

    function helpFor(i) {
        switch (rows[i]) {
        case "type":
            if (!block) return ""
            if (block.type === "series")     return "One show, playing until the block's time is up."
            if (block.type === "collection") return "A collection, taking turns through the shows in it."
            if (block.type === "genre")      return "Every film of this "
                                                    + genreWordOne.toLowerCase()
                                                    + ", so the block follows the library as it grows."
            if (block.type === "movie")      return "A film. Long enough for one, and the rest of the day starts where it ends."
            return "Anything this channel has gathered."
        case "source":  return block && String(block.name || "") === ""
                               ? "Nothing yet, so this block is a break."
                             : offCount() === 0
                               ? "What this block plays. Open it to leave parts of it out."
                               : "What this block plays, with " + offCount()
                                 + " of its parts left out of this block."
        case "length":  return root.hints.change + " changes it by "
                               + editRoot.step + " minutes — one slot of the plan's grid."
        case "order":   return shuffled()
                               ? "Episodes come in a shuffled order. The same shuffle every build, so a rebuild does not start the show over."
                               : "Episodes come in the order they first aired, carrying on from where the show got to."
        case "intros":  return foldersOf("intros").length === 0
                               ? "Plays into this block. Set one to play this show in as itself."
                               : "Plays into this block, instead of the channel's."
        case "outros":  return foldersOf("outros").length === 0
                               ? "Plays this block out. Set one to play this show out as itself."
                               : "Plays this block out, instead of the channel's."
        case "delete":  return "Remove this block. The day closes up behind it."
        }
        return ""
    }

    function shuffled() {
        return block ? String(block.order || "") === "shuffle" : false
    }

    function cycles(i) {
        return rows[i] === "type" || rows[i] === "length" || rows[i] === "order"
    }

    // Every change is one field of the block the screen is already holding, so
    // it is applied to a copy of that block. Building a fresh object instead
    // drops whatever the change was not thinking about, which is how a block
    // lost its own bumpers every time its length moved by half an hour.
    // startsAtMinute and the counts are worked out on the way out, not stored.
    function blockWith(changes) {
        var next = {}
        for (var k in block)
            if (k !== "startsAtMinute" && k !== "intros_count" && k !== "outros_count")
                next[k] = block[k]
        for (var c in changes) next[c] = changes[c]
        return next
    }

    // Every change is the same shape: take the plans, change one thing, hand
    // them back. The backend recomputes what each block's start time becomes.
    function writeBlock(changed) {
        var all = plans.slice()
        var mine = all[planIndex]
        var list = (mine.blocks || []).slice()
        list[blockIndex] = changed
        mine.blocks = list
        all[planIndex] = mine
        if (!virtualChannelsBackend.set_channel_plans(channelNumber, all)) {
            status = "Could not save that change"
            return false
        }
        status = ""
        reload()
        return true
    }

    function step_(delta) {
        if (!block) return
        var r = rows[current]

        if (r === "type") {
            // A block set to a type this source does not offer -- left behind
            // by a source change -- is still shown, and stepping moves off it
            // rather than sticking.
            var at = types.indexOf(block.type)
            if (at < 0) at = 0
            var next = types[(at + delta + types.length) % types.length]
            // The name and the id go: a series' name means nothing to a genre.
            // The bumpers stay, because they belong to the block, not to what
            // the block happens to draw on.
            var changed = { type: next, name: "", ref: "", minutes: block.minutes }
            // A movie block is a film's worth of time unless it has been set
            // otherwise, which is what makes it a movie slot by another name.
            if (next === "movie" && block.type !== "movie")
                changed.minutes = Math.max(block.minutes, 90)
            writeBlock(blockWith(changed))
            return
        }

        if (r === "order") {
            writeBlock(blockWith({ order: shuffled() ? "broadcast" : "shuffle" }))
            return
        }

        if (r === "length") {
            var mins = block.minutes + delta * step
            if (mins < step)        { status = "That is as short as a block goes"; return }
            if (mins > 24 * 60)     { status = "That is a whole day"; return }
            writeBlock(blockWith({ minutes: mins }))
        }
    }

    function open(i) {
        if (!block) return
        var r = rows[i]

        if (r === "delete") {
            if (!armedToDelete) { armedToDelete = true; status = ""; return }
            armedToDelete = false
            var all = plans.slice()
            var mine = all[planIndex]
            var list = (mine.blocks || []).slice()
            list.splice(blockIndex, 1)
            mine.blocks = list
            all[planIndex] = mine
            if (virtualChannelsBackend.set_channel_plans(channelNumber, all)) goBack()
            else status = "Could not remove that block"
            return
        }

        if (r === "intros" || r === "outros") {
            navigateTo("modules/virtual_channels/views/SourceIdents.qml", {
                moduleId:      editRoot.moduleId,
                channelNumber: editRoot.channelNumber,
                channelName:   editRoot.channelName,
                planIndex:     editRoot.planIndex,
                blockIndex:    editRoot.blockIndex,
                entryName:     editRoot.sourceLabel(),
                kind:          r
            }, { currentIndex: editRoot.current })
            return
        }

        if (r === "source") {
            var kind = block.type === "collection" ? "collections"
                     : block.type === "genre"      ? "moviegenres"
                     : block.type === "movie"      ? "movies"
                                                   : "shows"
            // The same list a pool row opens, told which block it is choosing
            // for. One thing at a time, and you can open what you chose to
            // leave parts of it out.
            navigateTo("modules/virtual_channels/views/SourceBrowser.qml", {
                moduleId:      editRoot.moduleId,
                channelNumber: editRoot.channelNumber,
                kind:          kind,
                title:         editRoot.channelName,
                planIndex:     editRoot.planIndex,
                blockIndex:    editRoot.blockIndex
            }, { currentIndex: editRoot.current })
            return
        }
    }


    Component.onCompleted: {
        reload()
        if (navListState.currentIndex !== undefined)
            current = Math.min(navListState.currentIndex, rowCount - 1)
    }

    // The same list every other settings screen is: the arrows beside a row
    // mean what they mean everywhere else, and the footer says what this row
    // does rather than what the screen can do.
    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: editRoot.moduleIcon
        title: editRoot.block
               ? (editRoot.clockLabel(editRoot.block.startsAtMinute) + " — " + editRoot.sourceLabel())
               : "Block"
        rows: editRoot.rows
        current: editRoot.current
        onCurrentChanged: { editRoot.current = current; editRoot.armedToDelete = false }
        status: editRoot.status
        labelFor: function(i) { return editRoot.labelFor(i) }
        valueFor: function(i) { return editRoot.valueFor(i) }
        helpFor:  function(i) { return editRoot.helpFor(i) }
        cycles:   function(i) { return editRoot.cycles(i) }
        actionFor: function(i) { return editRoot.rows[i] === "delete" ? "DELETE" : "CHOOSE" }
        onStep:     function(d) { editRoot.step_(d) }
        onActivate: function(i) { editRoot.open(i) }
        onBack:     function() { editRoot.goBack() }
    }
}
