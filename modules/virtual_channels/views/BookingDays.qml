import QtQuick
import Components

FocusScope {
    id: daysRoot
    property var navParams: ({})
    property string moduleId: navParams.moduleId || ""
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: ({})
    property int channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property int bookingIndex:  navParams.bookingIndex  !== undefined ? navParams.bookingIndex  : -1
    property string heading:    navParams.title || "Days"

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    readonly property var dayKeys:   ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
    readonly property var dayLabels: ["Monday", "Tuesday", "Wednesday", "Thursday",
                                      "Friday", "Saturday", "Sunday"]

    property var chosen: []
    property string status: ""

    readonly property int rowCount: 8
    property int current: 0

    focus: true

    function reload() {
        var all = virtualChannelsBackend.channel_bookings(channelNumber)
        if (bookingIndex < 0 || bookingIndex >= all.length) { goBack(); return }
        chosen = all[bookingIndex].days
    }

    function isOn(i) {
        if (i === 0) return chosen.length === 0
        return chosen.indexOf(dayKeys[i - 1]) >= 0
    }

    function save(next) {
        if (!virtualChannelsBackend.set_booking_days(channelNumber, bookingIndex, next)) {
            status = "Could not save"
            return
        }
        status = ""
        reload()
    }

    function toggle(i) {
        if (i === 0) { save([]); return }

        var key = dayKeys[i - 1]
        var next = chosen.slice()
        var at = next.indexOf(key)
        if (at >= 0) next.splice(at, 1)
        else         next.push(key)
        save(next)
    }

    Component.onCompleted: reload()

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
        } else if (event.key === Qt.Key_Up) {
            current = (current - 1 + rowCount) % rowCount
        } else if (event.key === Qt.Key_Down) {
            current = (current + 1) % rowCount
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            toggle(current)
        }
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: root.surfaceColor }

    AppBar {
        id: appBar
        iconSource: daysRoot.moduleIcon
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.075
        anchors.leftMargin: root.sw * 0.125
        title: daysRoot.heading + " — Days"
    }

    Column {
        id: form
        anchors.top: appBar.bottom
        anchors.topMargin: root.sh * 0.03
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75

        Repeater {
            model: daysRoot.rowCount
            delegate: Item {
                id: dayRow
                required property int index
                width: form.width
                height: root.sh * 0.055
                readonly property bool selected: index === daysRoot.current
                readonly property bool on: daysRoot.isOn(index)

                Rectangle {
                    anchors.fill: parent
                    color: dayRow.selected ? root.accentColor : "transparent"
                }

                Rectangle {
                    id: box
                    width: root.sh * 0.028
                    height: width
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: root.sw * 0.0125
                    color: dayRow.on ? (dayRow.selected ? root.surfaceColor : root.primaryColor)
                                     : "transparent"
                    border.color: dayRow.selected ? root.surfaceColor : root.tertiaryColor
                    border.width: 2
                }

                Text {
                    text: dayRow.index === 0 ? "Every Day"
                                             : daysRoot.dayLabels[dayRow.index - 1]
                    color: dayRow.selected ? root.surfaceColor
                                           : (dayRow.on ? root.primaryColor : root.tertiaryColor)
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: box.right
                    anchors.leftMargin: root.sw * 0.0187
                    font.pixelSize: root.sh * 0.0333
                }
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
            text: daysRoot.status !== "" ? daysRoot.status
                                         : "Ticked days are when this slot airs. Tick none for every day."
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
              + root.hints.select + ":TOGGLE"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.0833333
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
    }
}
