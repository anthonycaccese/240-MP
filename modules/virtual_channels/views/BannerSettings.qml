import QtQuick
import Components

FocusScope {
    id: bannerRoot

    focus: true
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: ({})
    property string moduleId: navParams.moduleId || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property bool showNumber: true
    property bool showName: true
    property real seconds: 1.5
    property int offsetX: 0
    property int offsetY: 0

    property string status: ""

    readonly property var rows: ["number", "name", "seconds", "offsetx", "offsety"]
    readonly property int rowCount: rows.length
    property int current: 0

    function reload() {
        var n  = appCore.get_setting(moduleId, "banner.number")
        var nm = appCore.get_setting(moduleId, "banner.name")
        var s  = parseFloat(appCore.get_setting(moduleId, "banner.seconds"))
        var x  = parseInt(appCore.get_setting(moduleId, "banner.offset_x"))
        var y  = parseInt(appCore.get_setting(moduleId, "banner.offset_y"))

        showNumber = (n !== "OFF")
        showName   = (nm !== "OFF")
        seconds    = isNaN(s) ? 1.5 : s
        offsetX    = isNaN(x) ? 0 : x
        offsetY    = isNaN(y) ? 0 : y
    }

    function save(key, value) {
        appCore.save_setting(moduleId, key, String(value))
        reload()
    }

    function offsetText(v) {
        if (v === 0) return "DEFAULT"
        return (v > 0 ? "+" : "") + v + "%"
    }

    function labelFor(i) {
        switch (rows[i]) {
        case "number":  return "Show Number"
        case "name":    return "Show Name"
        case "seconds": return "Show For"
        case "offsetx": return "Horizontal"
        case "offsety": return "Vertical"
        }
        return ""
    }

    function valueFor(i) {
        switch (rows[i]) {
        case "number":  return showNumber ? "ON" : "OFF"
        case "name":    return showName ? "ON" : "OFF"
        case "seconds": return seconds.toFixed(1) + "s"
        case "offsetx": return offsetText(offsetX)
        case "offsety": return offsetText(offsetY)
        }
        return ""
    }

    function helpFor(i) {
        switch (rows[i]) {
        case "number":  return "The channel number, in large type."
        case "name":    return "The channel name, under the number."
        case "seconds": return "How long the banner stays on screen after a change."
        case "offsetx": return "Nudge left or right. Increase if a CRT is cutting off the right edge."
        case "offsety": return "Nudge up or down. Increase if a CRT is cutting off the top."
        }
        return ""
    }

    function cycles(i) { return true }

    function open(i) { }

    function step(delta) {
        switch (rows[current]) {
        case "number":
            save("banner.number", showNumber ? "OFF" : "ON"); break
        case "name":
            save("banner.name", showName ? "OFF" : "ON"); break
        case "seconds": {
            var s = Math.round((seconds + delta * 0.5) * 10) / 10
            save("banner.seconds", Math.max(0.5, Math.min(10.0, s)))
            break
        }
        case "offsetx":
            save("banner.offset_x", Math.max(-20, Math.min(20, offsetX + delta)))
            break
        case "offsety":
            save("banner.offset_y", Math.max(-20, Math.min(20, offsetY + delta)))
            break
        }
    }

    Component.onCompleted: reload()

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: bannerRoot.moduleIcon
        title: "Channel Banner"
        rows: bannerRoot.rows
        current: bannerRoot.current
        onCurrentChanged: bannerRoot.current = current
        status: bannerRoot.status
        labelFor: function(i) { return bannerRoot.labelFor(i) }
        valueFor: function(i) { return bannerRoot.valueFor(i) }
        helpFor:  function(i) { return bannerRoot.helpFor(i) }
        cycles:   function(i) { return bannerRoot.cycles(i) }
        onStep:     function(d) { bannerRoot.step(d) }
        onActivate: function(i) { bannerRoot.open(i) }
        onBack:     function() { bannerRoot.goBack() }
    }
}
