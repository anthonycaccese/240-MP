import QtQuick
import Components

// A day, as an ordered stack of blocks.
//
// Nothing here has a start time typed into it: the order and the lengths say
// when everything airs, which is what makes a gap or an overlap impossible to
// write rather than something this screen has to police. Left and right move a
// block, exactly as they move a channel on Manage.
FocusScope {
    id: planRoot
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""

    property string moduleId:      navParams.moduleId || ""
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""
    property int    planIndex:     navParams.planIndex !== undefined ? navParams.planIndex : 0
    property var    navListState:  navParams.navListState  || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var plans: []
    property string status: ""
    property int current: 0

    readonly property var plan: plans.length > planIndex ? plans[planIndex] : null
    readonly property var blocks: plan ? (plan.blocks || []) : []

    // Row zero is the day being shown. Left and right page between them, which
    // is the same rule every other row follows: left and right change the row
    // you are on.
    readonly property int planRow:  0
    readonly property int addIndex: blocks.length + 1
    readonly property int rowCount: blocks.length + 2

    function blockAt(i) { return (i > planRow && i < addIndex) ? blocks[i - 1] : null }

    focus: true

    function reload() {
        plans = virtualChannelsBackend.channel_plans(channelNumber)
        if (current >= rowCount) current = rowCount - 1
        if (current < 0) current = 0
    }

    function lengthLabel(mins) {
        var h = Math.floor(mins / 60)
        var m = mins % 60
        if (h > 0 && m > 0) return h + "H " + (m < 10 ? "0" + m : m) + "M"
        if (h > 0)          return h + "H"
        return m + "M"
    }

    // A block nobody has filled in yet is not a mistake: it says so, and holds
    // the card for its length.
    function sourceLabel(b) {
        if (b.type === "movie")  return b.name !== "" ? b.name.toUpperCase() : "ANY MOVIE"
        if (b.type === "random") return "ANYTHING"
        if (b.name === "")       return "NO CONTENT"
        return b.name.toUpperCase()
    }

    function clockLabel(mins) {
        var h = Math.floor(mins / 60)
        var m = mins % 60
        var suffix = h < 12 ? "AM" : "PM"
        var hh = h % 12
        if (hh === 0) hh = 12
        return hh + ":" + (m < 10 ? "0" + m : m) + " " + suffix
    }

    function labelFor(i) {
        if (i === planRow)  return "Day"
        if (i === addIndex) return "Add A Block"
        var b = blockAt(i)
        return b ? clockLabel(b.startsAtMinute) + "  " + sourceLabel(b) : ""
    }

    function valueFor(i) {
        if (i === planRow) {
            if (!plan) return ""
            var name = plan.name !== "" ? plan.name.toUpperCase() : "DAY"
            return plans.length > 1 ? "◄ " + name + " ►" : name
        }
        if (i === addIndex) return ""
        var b = blockAt(i)
        return b ? lengthLabel(b.minutes) : ""
    }

    function helpFor(i) {
        if (i === planRow)
            return plans.length > 1
                   ? root.hints.change + " picks which day you are laying out. A day with no blocks is all breaks."
                   : "The day these blocks cover."
        if (i === addIndex)
            return "A new block at the end of the day. Open it to say what it plays."
        var b = blockAt(i)
        if (!b) return ""
        if (b.type !== "movie" && b.type !== "random" && b.name === "")
            return "Nothing in it yet, so this is a break. Open it to say what it plays."
        return root.hints.change + " moves it up and down the day · "
               + root.hints.select + " opens it"
    }

    // Reordering is this screen handing back the list it was given, swapped.
    // Every start time below the moved block follows from the new order, so
    // nothing has to be recalculated here.
    function move(delta) {
        if (current === planRow) {
            if (plans.length < 2) return
            planIndex = (planIndex + delta + plans.length) % plans.length
            status = ""
            return
        }
        if (current >= addIndex) { status = "That is not a block"; return }
        var from = current - 1
        var to = from + delta
        if (to < 0 || to >= blocks.length) {
            status = delta < 0 ? "Already first" : "Already last"
            return
        }
        var all = plans.slice()
        var mine = all[planIndex]
        var list = (mine.blocks || []).slice()
        var held = list[from]
        list[from] = list[to]
        list[to] = held
        mine.blocks = list
        all[planIndex] = mine
        if (!virtualChannelsBackend.set_channel_plans(channelNumber, all)) {
            status = "Could not move that block"
            return
        }
        status = ""
        current = to + 1
        reload()
    }

    function addBlock() {
        var all = plans.slice()
        var mine = all[planIndex]
        var list = (mine.blocks || []).slice()
        list.push({ type: "series", name: "", ref: "",
                    minutes: plan && plan.gridMinutes > 0 ? plan.gridMinutes : 30 })
        mine.blocks = list
        all[planIndex] = mine
        if (!virtualChannelsBackend.set_channel_plans(channelNumber, all)) {
            status = "Could not add a block"
            return
        }
        reload()
        current = blocks.length
        openBlock(current)
    }

    // Which day is being laid out is this screen's own state, so it has to
    // travel with the list position. Without it, coming back from a block lands
    // on the first day -- and the next block added would go onto a day nobody
    // chose, which is a wrong that says nothing as it happens.
    function openBlock(i) {
        navigateTo("modules/virtual_channels/views/BlockEdit.qml", {
            moduleId:      planRoot.moduleId,
            channelNumber: planRoot.channelNumber,
            channelName:   planRoot.channelName,
            planIndex:     planRoot.planIndex,
            blockIndex:    i - 1
        }, { currentIndex: i, planIndex: planRoot.planIndex })
    }

    function open(i) {
        if (i === planRow)  return
        if (i === addIndex) { addBlock(); return }
        openBlock(i)
    }

    Component.onCompleted: {
        if (navListState.planIndex !== undefined) planIndex = navListState.planIndex
        reload()
        if (planIndex >= plans.length) planIndex = 0
        if (navListState.currentIndex !== undefined)
            current = Math.min(navListState.currentIndex, rowCount - 1)
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
        } else if (event.key === Qt.Key_Up) {
            current = (current - 1 + rowCount) % rowCount
            status = ""
        } else if (event.key === Qt.Key_Down) {
            current = (current + 1) % rowCount
            status = ""
        } else if (event.key === Qt.Key_Left) {
            move(-1)
        } else if (event.key === Qt.Key_Right) {
            move(1)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            open(current)
        }
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: root.surfaceColor }

    AppBar {
        id: appBar
        iconSource: planRoot.moduleIcon
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.075
        anchors.leftMargin: root.sw * 0.125
        title: planRoot.plan ? (planRoot.plan.name !== "" ? planRoot.plan.name : "Blocks")
                             : "Blocks"
    }

    // What the day adds up to, said out loud. A plan shorter than the day comes
    // round again rather than leaving the channel dark, and this is where that
    // is admitted to rather than left to be discovered on air.
    Text {
        id: coverage
        anchors.top: appBar.bottom
        anchors.topMargin: root.sh * 0.015
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75
        horizontalAlignment: Text.AlignRight
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0271
        text: {
            if (!planRoot.plan) return ""
            var mins = planRoot.plan.totalMinutes || 0
            if (mins <= 0) return "NOTHING PLANNED YET"
            var full = 24 * 60
            if (mins >= full) return "FILLS THE DAY"
            return planRoot.lengthLabel(mins) + " PLANNED · REST IS A BREAK"
        }
    }

    Text {
        anchors.centerIn: parent
        visible: planRoot.blocks.length === 0
        text: "NO BLOCKS YET"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0333
    }

    ListView {
        id: rowList
        anchors.top: coverage.bottom
        anchors.topMargin: root.sh * 0.015
        anchors.bottom: helpBackground.top
        anchors.bottomMargin: root.sh * 0.02
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75
        clip: true
        interactive: false
        model: planRoot.rowCount
        currentIndex: planRoot.current
        highlightMoveDuration: 0

        delegate: Item {
            required property int index
            width: rowList.width
            height: root.sh * 0.07
            readonly property bool selected: index === planRoot.current
            readonly property bool isAction: index >= planRoot.addIndex
                                         || index === planRoot.planRow

            Rectangle {
                anchors.fill: parent
                color: parent.selected ? root.accentColor : "transparent"
            }

            Text {
                text: planRoot.labelFor(parent.index)
                color: parent.selected ? root.surfaceColor
                                       : (parent.isAction ? root.tertiaryColor : root.primaryColor)
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0125
                anchors.right: lengthText.left
                anchors.rightMargin: root.sw * 0.0125
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.0354
            }

            Text {
                id: lengthText
                text: planRoot.valueFor(parent.index)
                color: parent.selected ? root.surfaceColor : root.tertiaryColor
                font.family: root.globalFont
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.0125
                font.pixelSize: root.sh * 0.0271
            }
        }
    }

    Rectangle {
        id: helpBackground
        property color baseColor: root.primaryColor
        color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.2)
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1583333
        anchors.leftMargin: root.sw * 0.125
        width: root.sw * 0.75
        height: root.sh * 0.0583333
        clip: true

        Text {
            text: planRoot.status !== "" ? planRoot.status : planRoot.helpFor(planRoot.current)
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0291667
            wrapMode: Text.WordWrap
            anchors.fill: parent
            anchors.margins: root.sw * 0.0125
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Text {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + root.hints.change + ":MOVE " + root.hints.select + ":OPEN"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.0833333
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
    }
}
