import QtQuick
import Components

Item {
    id: optionList

    property var rows: []
    property string title: ""
    property url iconSource: ""

    property var labelFor: function(i) { return "" }
    property var valueFor: function(i) { return "" }
    property var helpFor:  function(i) { return "" }
    property var cycles:   function(i) { return false }
    property var actionFor: function(i) { return "OPEN" }
    property var secondaryFor: function(i) { return "" }

    property string status: ""
    property bool busy: false
    property string emptyText: ""

    property int current: 0
    readonly property int count: rows.length

    signal step(int delta)
    signal activate(int index)
    signal secondary(int index)
    signal back()

    function clampCurrent() {
        if (current >= count) current = Math.max(0, count - 1)
    }
    onRowsChanged: clampCurrent()

    focus: true

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
            || event.key === Qt.Key_Back) {
            optionList.back()
        } else if (busy || count === 0) {
        } else if (event.key === Qt.Key_Up) {
            current = (current - 1 + count) % count
        } else if (event.key === Qt.Key_Down) {
            current = (current + 1) % count
        } else if (event.key === Qt.Key_Left) {
            if (cycles(current))                      optionList.step(-1)
            else if (secondaryFor(current) !== "")    optionList.secondary(current)
        } else if (event.key === Qt.Key_Right) {
            if (cycles(current))                      optionList.step(1)
            else if (secondaryFor(current) !== "")    optionList.secondary(current)
            else                                      optionList.activate(current)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (cycles(current)) optionList.step(1)
            else                 optionList.activate(current)
        }
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: root.surfaceColor }

    AppBar {
        id: appBar
        iconSource: optionList.iconSource
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.075
        anchors.leftMargin: root.sw * 0.125
        title: optionList.title
    }

    Text {
        anchors.centerIn: parent
        visible: optionList.count === 0 && optionList.emptyText !== ""
        text: optionList.emptyText
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0333
    }

    ListView {
        id: form
        anchors.top: appBar.bottom
        anchors.topMargin: root.sh * 0.03
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75
        height: Math.max(0, helpBackground.y - y)
        clip: true
        interactive: false
        currentIndex: optionList.current
        highlightMoveDuration: 0
        highlightFollowsCurrentItem: true

        readonly property real rowH:
            Math.max(root.sh * 0.045,
                     Math.min(root.sh * 0.065,
                              height / Math.max(1, optionList.count)))

        model: optionList.count
        delegate: Item {
            id: row
            required property int index
            width: form.width
            height: form.rowH
            readonly property bool selected: index === optionList.current
            readonly property bool steps: optionList.cycles(index)

            Rectangle {
                anchors.fill: parent
                color: row.selected ? root.accentColor : "transparent"
            }

            Text {
                text: optionList.labelFor(row.index)
                color: row.selected ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0125
                width: Math.max(root.sw * 0.15,
                                row.width - valueRow.width - root.sw * 0.03)
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.0354
            }

            Row {
                id: valueRow
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.0125
                spacing: root.sw * 0.00625

                Text {
                    visible: row.steps
                    text: "◄"
                    color: row.selected ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0292
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: optionList.valueFor(row.index)
                    color: row.selected ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "►"
                    color: row.selected ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0292
                    anchors.verticalCenter: parent.verticalCenter
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
            text: optionList.status !== "" ? optionList.status
                                           : optionList.helpFor(optionList.current)
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0270833
            wrapMode: Text.WordWrap
            anchors.fill: parent
            anchors.margins: root.sw * 0.0125
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Text {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + (optionList.secondaryFor(optionList.current) !== ""
                 ? root.hints.change + ":" + optionList.secondaryFor(optionList.current) + " "
                 : "")
              + (optionList.count > 0 && optionList.cycles(optionList.current)
                 ? root.hints.change + ":CHANGE"
                 : root.hints.select + ":" + optionList.actionFor(optionList.current))
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.0833333
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
    }
}
