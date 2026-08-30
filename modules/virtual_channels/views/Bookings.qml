import QtQuick
import Components

FocusScope {
    id: bookingsRoot
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""
    property var    navListState:  navParams.navListState  || ({})

    property string moduleId: navParams.moduleId || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var bookings: []
    property string status: ""
    property bool building: false

    readonly property int addIndex:     bookings.length
    readonly property int rebuildIndex: bookings.length + 1
    readonly property int rowCount:     bookings.length + 2

    property int current: 0

    focus: true

    function reload() {
        bookings = virtualChannelsBackend.channel_bookings(channelNumber)
        if (current >= rowCount) current = rowCount - 1
        if (current < 0) current = 0
    }

    function timeLabel(h, m) {
        var suffix = h < 12 ? "AM" : "PM"
        var hh = h % 12
        if (hh === 0) hh = 12
        return hh + ":" + (m < 10 ? "0" + m : m) + " " + suffix
    }

    function daysLabel(b) {
        if (b.everyDay) return "EVERY DAY"
        var order = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
        var out = []
        for (var i = 0; i < order.length; i++)
            if (b.days.indexOf(order[i]) >= 0) out.push(order[i].toUpperCase())
        return out.length > 0 ? out.join(" ") : "NEVER"
    }

    function poolLabel(b) {
        if (b.source === "local")
            return b.folder === "" ? "NO FOLDER" : b.folder.split("/").pop().toUpperCase()
        if (b.criteria === 0) return b.anyFilm ? "ANY MOVIE" : "NOTHING YET"

        var bits = []
        if (b.films > 0)             bits.push(b.films === 1 ? "1 MOVIE" : b.films + " MOVIES")
        var word = b.source === "plex" ? "CATEGORIE" : "GENRE"
        if (b.genres.length > 0)     bits.push(b.genres.length === 1 ? "1 " + word.replace("CATEGORIE", "CATEGORY")
                                                                     : b.genres.length + " " + word + "S")
        if (b.collections.length > 0) bits.push(b.collections.length === 1 ? "1 SET"
                                                                          : b.collections.length + " SETS")
        if (b.playlists.length > 0)  bits.push(b.playlists.length === 1 ? "1 LIST"
                                                                       : b.playlists.length + " LISTS")
        if (b.match.length > 0)      bits.push(b.match.join(" / ").toUpperCase())
        return bits.join(" · ")
    }

    function labelFor(i) {
        if (i === addIndex)     return "Add Movie Slot"
        if (i === rebuildIndex) return building ? "Rebuilding…" : "Rebuild This Channel"
        var b = bookings[i]
        return timeLabel(b.hour, b.minute) + "  " + b.name
    }

    function valueFor(i) {
        if (i >= addIndex) return ""
        var b = bookings[i]
        return daysLabel(b) + " · " + poolLabel(b)
    }

    function helpFor(i) {
        if (i === addIndex)
            return "A new slot at 8 PM every day. Open it to say what it plays."
        if (i === rebuildIndex)
            return "Rebuild the schedule so slot changes actually air."
        var b = bookings[i]
        if (!b.valid) return "This slot has no usable time and will not air. Open it to set one."
        if (!b.anyFilm && b.criteria === 0)
            return "Nothing picked yet, so this slot will not air. Open it to choose movies."
        return "Open to change its time, its days, or which movies it draws on."
    }

    function open(i) {
        if (building) return

        if (i === rebuildIndex) {
            building = true
            status = "Rebuilding…"
            virtualChannelsBackend.regenerate(channelNumber)
            return
        }

        if (i === addIndex) {
            var made = virtualChannelsBackend.add_booking(channelNumber)
            if (made < 0) { status = "Could not add another slot"; return }
            reload()
            current = made
            openBooking(made)
            return
        }
        openBooking(i)
    }

    function openBooking(i) {
        navigateTo("modules/virtual_channels/views/BookingEdit.qml", {
            moduleId:      bookingsRoot.moduleId,
            channelNumber: bookingsRoot.channelNumber,
            channelName:   bookingsRoot.channelName,
            bookingIndex:  i
        }, { currentIndex: i })
    }

    Component.onCompleted: {
        reload()
        if (navListState.currentIndex !== undefined)
            current = Math.min(navListState.currentIndex, rowCount - 1)
    }

    Connections {
        target: virtualChannelsBackend
        function onGenerationProgress(ch, done, total) {
            if (ch !== bookingsRoot.channelNumber) return
            bookingsRoot.status = total > 0 ? "Building… " + done + " / " + total : "Building…"
        }
        function onGenerationFinished(ch, ok, message) {
            if (ch !== bookingsRoot.channelNumber) return
            bookingsRoot.building = false
            bookingsRoot.status = (ok ? "Rebuilt: " : "Failed: ") + message
            bookingsRoot.reload()
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
        } else if (event.key === Qt.Key_Up) {
            if (!building) current = (current - 1 + rowCount) % rowCount
        } else if (event.key === Qt.Key_Down) {
            if (!building) current = (current + 1) % rowCount
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Right) {
            open(current)
        }
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: root.surfaceColor }

    AppBar {
        id: appBar
        iconSource: bookingsRoot.moduleIcon
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.075
        anchors.leftMargin: root.sw * 0.125
        title: bookingsRoot.channelName + " — Movie Slots"
    }

    Text {
        anchors.centerIn: parent
        visible: bookingsRoot.bookings.length === 0
        text: "NO MOVIE SLOTS YET"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0333
    }

    ListView {
        id: rowList
        anchors.top: appBar.bottom
        anchors.topMargin: root.sh * 0.025
        anchors.bottom: helpBackground.top
        anchors.bottomMargin: root.sh * 0.02
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75
        clip: true
        interactive: false
        model: bookingsRoot.rowCount
        currentIndex: bookingsRoot.current
        highlightMoveDuration: 0

        delegate: Item {
            required property int index
            width: rowList.width
            height: root.sh * 0.07
            readonly property bool selected: index === bookingsRoot.current
            readonly property bool isAction: index >= bookingsRoot.addIndex

            Rectangle {
                anchors.fill: parent
                color: parent.selected ? root.accentColor : "transparent"
            }

            Text {
                text: bookingsRoot.labelFor(parent.index)
                color: parent.selected ? root.surfaceColor
                                       : (parent.isAction ? root.tertiaryColor : root.primaryColor)
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0125
                anchors.right: valueText.left
                anchors.rightMargin: root.sw * 0.0125
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.0354
            }

            Text {
                id: valueText
                text: bookingsRoot.valueFor(parent.index)
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
            text: bookingsRoot.status !== "" ? bookingsRoot.status
                                             : bookingsRoot.helpFor(bookingsRoot.current)
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
              + root.hints.select + ":OPEN"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.0833333
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
    }
}
