import QtQuick

// The two-row strip along the bottom of every WeatherStar screen.
//
// Row 1 is fixed: abbreviated day/date on the left, running clock on the right.
// Row 2 rotates through short data lines independently of the main screen —
// that independent rotation is a lot of the character of the original.
//
// The clock runs on the *location's* UTC offset, not the device's, so a forecast
// for somewhere else shows that place's time.
Item {
    id: statusBar

    property int    utcOffsetSeconds: 0
    property bool   twelveHour: false
    // Short strings to cycle through on the second row.
    property var    lines: []
    property int    lineIntervalMs: 5000

    property int _lineIndex: 0
    property string _date: ""
    property string _time: ""

    implicitHeight: dateRow.height + dataRow.height + (root.sh * 0.012)

    function _pad(n) { return n < 10 ? "0" + n : "" + n }

    function _tick() {
        // Shift real UTC by the location's offset. getTimezoneOffset() is in
        // minutes and positive west of UTC, hence the addition.
        var now = new Date()
        var loc = new Date(now.getTime()
                           + utcOffsetSeconds * 1000
                           + now.getTimezoneOffset() * 60000)

        var days   = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
        var months = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
        _date = days[loc.getDay()] + " " + months[loc.getMonth()] + " " + loc.getDate()

        var h = loc.getHours()
        var suffix = ""
        if (twelveHour) {
            suffix = h < 12 ? " AM" : " PM"
            h = h % 12
            if (h === 0) h = 12
            _time = h + ":" + _pad(loc.getMinutes()) + ":" + _pad(loc.getSeconds()) + suffix
        } else {
            _time = _pad(h) + ":" + _pad(loc.getMinutes()) + ":" + _pad(loc.getSeconds())
        }
    }

    Component.onCompleted: _tick()

    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: statusBar._tick()
    }

    Timer {
        interval: statusBar.lineIntervalMs
        running: statusBar.lines.length > 1
        repeat: true
        onTriggered: statusBar._lineIndex = (statusBar._lineIndex + 1) % statusBar.lines.length
    }

    Rectangle {
        id: divider
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.max(1, root.sh * 0.0041667)//2
        color: root.tertiaryColor
        opacity: 0.5
    }

    Item {
        id: dateRow
        anchors.top: divider.bottom
        anchors.topMargin: root.sh * 0.008
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.sh * 0.05//24

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: statusBar._date
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0375//18
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: statusBar._time
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0375//18
        }
    }

    Text {
        id: dataRow
        anchors.top: dateRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.sh * 0.0583333//28
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: statusBar.lines.length > 0
              ? statusBar.lines[statusBar._lineIndex % statusBar.lines.length] : ""
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.05//24
    }
}
