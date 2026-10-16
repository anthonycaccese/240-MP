import QtQuick

// Digits pressed on a remote, gathered into a channel number the way a set
// does it: the number goes when Enter is pressed, when nothing more has been
// typed for a moment, or when no further digit could name a channel that
// exists. A number no channel has is dropped and nothing changes.
QtObject {
    id: entry

    property var dial: []            // the channel numbers there are
    property int settleMs: 1500
    property string digits: ""

    signal typed(string digits)      // what has been typed so far
    signal chosen(int number)
    signal dropped()

    property Timer settle: Timer {
        interval: entry.settleMs
        onTriggered: entry.commit()
    }

    // Digits, and Enter or Back while digits are pending, are this object's.
    // Anything else is the caller's; the answer says which. A held key repeats,
    // and one press of 6 must not become 66.
    function press(key, autoRepeat) {
        if (key >= Qt.Key_0 && key <= Qt.Key_9) {
            if (autoRepeat) return true
            digits += String(key - Qt.Key_0)
            typed(digits)
            if (digits.length >= longest()) commit()
            else settle.restart()
            return true
        }
        if (digits === "") return false
        if (key === Qt.Key_Return || key === Qt.Key_Enter) { commit(); return true }
        if (key === Qt.Key_Escape || key === Qt.Key_Backspace || key === Qt.Key_Back) {
            clear()
            return true
        }
        return false
    }

    function longest() {
        var n = 0
        for (var i = 0; i < dial.length; i++) n = Math.max(n, String(dial[i]).length)
        return Math.max(1, n)
    }

    // Whether the number was a channel.
    function commit() {
        settle.stop()
        var number = parseInt(digits, 10)
        digits = ""
        if (dial.indexOf(number) >= 0) { chosen(number); return true }
        dropped()
        return false
    }

    function clear() {
        settle.stop()
        digits = ""
        typed("")
    }
}
