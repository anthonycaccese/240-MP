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

    readonly property int addIndex:  blocks.length
    readonly property int rowCount:  blocks.length + 1

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

    function sourceLabel(b) {
        if (b.type === "movie")      return b.name !== "" ? b.name.toUpperCase() : "ANY MOVIE"
        if (b.type === "random")     return "ANYTHING"
        if (b.name === "")           return "NOTHING PICKED"
        return b.name.toUpperCase()
    }

    function labelFor(i) {
        if (i === addIndex) return "Add A Block"
        var b = blocks[i]
        return b.startsAt + "  " + sourceLabel(b)
    }

    function valueFor(i) {
        if (i === addIndex) return ""
        return lengthLabel(blocks[i].minutes)
    }

    function helpFor(i) {
        if (i === addIndex)
            return "A new half hour at the end of the day. Open it to say what it plays."
        var b = blocks[i]
        if (b.type !== "movie" && b.type !== "random" && b.name === "")
            return "Nothing picked, so this block holds the card. Open it to choose what it plays."
        return root.hints.change + " moves it up and down the day · "
               + root.hints.select + " opens it"
    }

    // Reordering is this screen handing back the list it was given, swapped.
    // Every start time below the moved block follows from the new order, so
    // nothing has to be recalculated here.
    function move(delta) {
        if (current >= addIndex) { status = "That is not a block"; return }
        var to = current + delta
        if (to < 0 || to >= blocks.length) {
            status = delta < 0 ? "Already first" : "Already last"
            return
        }
        var all = plans.slice()
        var mine = all[planIndex]
        var list = (mine.blocks || []).slice()
        var held = list[current]
        list[current] = list[to]
        list[to] = held
        mine.blocks = list
        all[planIndex] = mine
        if (!virtualChannelsBackend.set_channel_plans(channelNumber, all)) {
            status = "Could not move that block"
            return
        }
        status = ""
        current = to
        reload()
    }

    function addBlock() {
        var all = plans.slice()
        var mine = all[planIndex]
        var list = (mine.blocks || []).slice()
        list.push({ type: "series", name: "", ref: "", minutes: plan ? plan.gridMinutes : 30 })
        mine.blocks = list
        all[planIndex] = mine
        if (!virtualChannelsBackend.set_channel_plans(channelNumber, all)) {
            status = "Could not add a block"
            return
        }
        reload()
        current = blocks.length - 1
        openBlock(current)
    }

    function openBlock(i) {
        navigateTo("modules/virtual_channels/views/BlockEdit.qml", {
            moduleId:      planRoot.moduleId,
            channelNumber: planRoot.channelNumber,
            channelName:   planRoot.channelName,
            planIndex:     planRoot.planIndex,
            blockIndex:    i
        }, { currentIndex: i })
    }

    function open(i) {
        if (i === addIndex) { addBlock(); return }
        openBlock(i)
    }

    Component.onCompleted: {
        reload()
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
        title: planRoot.plan ? (planRoot.plan.name !== "" ? planRoot.plan.name : "Day Plan")
                             : "Day Plan"
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
            if (mins <= 0) return "NOTHING AIRS YET"
            var full = 24 * 60
            if (mins >= full) return planRoot.lengthLabel(mins) + " — FILLS THE DAY"
            var times = Math.floor(full / mins)
            return planRoot.lengthLabel(mins) + " — COMES ROUND " + times + "× A DAY"
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
