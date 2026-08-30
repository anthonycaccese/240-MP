import QtQuick
import Components

FocusScope {
    id: logoRoot

    focus: true

    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: ({})
    property string moduleId: navParams.moduleId || ""
    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string file: ""
    property real sizePct: 12
    property real opacityPct: 70
    property int offsetX: 0
    property int offsetY: 0
    property string status: ""

    readonly property var rows: ["logo", "size", "opacity", "offsetx", "offsety", "preview"]
    readonly property int rowCount: rows.length
    property int current: 0

    function reload() {
        file       = appCore.get_setting(moduleId, "logo.file") || ""
        var sz     = parseFloat(appCore.get_setting(moduleId, "logo.size"))
        var op     = parseFloat(appCore.get_setting(moduleId, "logo.opacity"))
        var ox     = parseInt(appCore.get_setting(moduleId, "logo.offset_x"))
        var oy     = parseInt(appCore.get_setting(moduleId, "logo.offset_y"))
        sizePct    = isNaN(sz) ? 12 : sz
        opacityPct = isNaN(op) ? 70 : op
        offsetX    = isNaN(ox) ? 0 : ox
        offsetY    = isNaN(oy) ? 0 : oy
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
        case "logo":    return "Default Logo"
        case "size":    return "Size"
        case "opacity": return "Opacity"
        case "offsetx": return "Horizontal"
        case "offsety": return "Vertical"
        case "preview": return "Preview"
        }
        return ""
    }

    function valueFor(i) {
        switch (rows[i]) {
        case "logo":    return file === "" ? "NONE" : file.replace(/\.[^.]+$/, "").toUpperCase()
        case "size":    return sizePct + "%"
        case "opacity": return opacityPct + "%"
        case "offsetx": return offsetText(offsetX)
        case "offsety": return offsetText(offsetY)
        }
        return ""
    }

    function helpFor(i) {
        switch (rows[i]) {
        case "logo":    return "Used by channels that have not chosen one. Drawing one costs about a third of decode speed."
        case "size":    return "How wide the logo is, as a share of the picture."
        case "opacity": return "Solid at 100%. A station bug is usually faint."
        case "offsetx": return "Nudge left or right. Increase if a CRT is cutting off the right edge."
        case "offsety": return "Nudge up or down. Increase if a CRT is cutting off the top."
        case "preview": return "See it drawn on a blank screen, exactly as it will air."
        }
        return ""
    }

    function cycles(i) { return rows[i] !== "logo" && rows[i] !== "preview" }

    function step(delta) {
        switch (rows[current]) {
        case "size":
            save("logo.size", Math.max(4, Math.min(33, sizePct + delta)))
            break
        case "opacity":
            save("logo.opacity", Math.max(10, Math.min(100, opacityPct + delta * 5)))
            break
        case "offsetx":
            save("logo.offset_x", Math.max(-20, Math.min(20, offsetX + delta)))
            break
        case "offsety":
            save("logo.offset_y", Math.max(-20, Math.min(20, offsetY + delta)))
            break
        }
    }

    function open(i) {
        if (rows[i] === "logo") {
            navigateTo("modules/virtual_channels/views/LogoPicker.qml", {
                moduleId: logoRoot.moduleId,
                settingKey: "logo.file"
            }, { currentIndex: logoRoot.current })
        } else if (rows[i] === "preview") {
            navigateTo("modules/virtual_channels/views/LogoPreview.qml", {
                moduleId: logoRoot.moduleId
            }, { currentIndex: logoRoot.current })
        }
    }

    Component.onCompleted: reload()

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: logoRoot.moduleIcon
        title: "Channel Logo"
        rows: logoRoot.rows
        current: logoRoot.current
        onCurrentChanged: logoRoot.current = current
        status: logoRoot.status
        labelFor: function(i) { return logoRoot.labelFor(i) }
        valueFor: function(i) { return logoRoot.valueFor(i) }
        helpFor:  function(i) { return logoRoot.helpFor(i) }
        cycles:   function(i) { return logoRoot.cycles(i) }
        onStep:     function(d) { logoRoot.step(d) }
        onActivate: function(i) { logoRoot.open(i) }
        onBack:     function() { logoRoot.goBack() }
    }
}
