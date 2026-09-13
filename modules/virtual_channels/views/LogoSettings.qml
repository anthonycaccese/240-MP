import QtQuick
import Components

// The module's Channel Logo screen, and every channel's own. Given a channel
// it reads and writes that channel's file and style, and a value the channel
// has not set shows the module's, which is what will be drawn.
FocusScope {
    id: logoRoot

    focus: true

    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: ({})
    property string moduleId: navParams.moduleId || ""
    property int channelNumber: navParams.channelNumber !== undefined
                                ? navParams.channelNumber : -1
    readonly property bool perChannel: channelNumber >= 0
    property string heading: navParams.title || ""
    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string file: ""
    property real sizePct: 12
    property real opacityPct: 70
    property int offsetX: 0
    property int offsetY: 0
    property var own: ({})           // the channel's own style, where it has one
    property string status: ""

    readonly property var rows: perChannel
        ? ["logo", "size", "opacity", "offsetx", "offsety", "follow", "preview"]
        : ["logo", "size", "opacity", "offsetx", "offsety", "preview"]
    readonly property int rowCount: rows.length
    property int current: 0

    function moduleValue(key) { return appCore.get_setting(moduleId, "logo." + key) }
    function effective(key) {
        return perChannel && own[key] !== undefined ? own[key] : moduleValue(key)
    }
    function isOwn(key) { return perChannel && own[key] !== undefined }

    function reload() {
        own  = perChannel ? (virtualChannelsBackend.channel_logo_style(channelNumber) || ({})) : ({})
        file = perChannel ? (virtualChannelsBackend.channel_logo(channelNumber) || "")
                          : (moduleValue("file") || "")
        var sz     = parseFloat(effective("size"))
        var op     = parseFloat(effective("opacity"))
        var ox     = parseInt(effective("offset_x"))
        var oy     = parseInt(effective("offset_y"))
        sizePct    = isNaN(sz) ? 12 : sz
        opacityPct = isNaN(op) ? 70 : op
        offsetX    = isNaN(ox) ? 0 : ox
        offsetY    = isNaN(oy) ? 0 : oy
    }

    function save(key, value) {
        if (perChannel) virtualChannelsBackend.set_channel_logo_style(channelNumber, key, value)
        else            appCore.save_setting(moduleId, "logo." + key, String(value))
        reload()
    }

    function offsetText(v) {
        if (v === 0) return "DEFAULT"
        return (v > 0 ? "+" : "") + v + "%"
    }

    // A channel's row says when the value is the module's rather than its own.
    function marked(key, text) { return perChannel && !isOwn(key) ? text + " (CHANNELS)" : text }

    function labelFor(i) {
        switch (rows[i]) {
        case "logo":    return perChannel ? "Logo" : "Default Logo"
        case "size":    return "Size"
        case "opacity": return "Opacity"
        case "offsetx": return "Horizontal"
        case "offsety": return "Vertical"
        case "follow":  return "Follow Channels Settings"
        case "preview": return "Preview"
        }
        return ""
    }

    function valueFor(i) {
        switch (rows[i]) {
        case "logo":    return file === "" ? (perChannel ? "DEFAULT" : "NONE")
                                           : file.replace(/\.[^.]+$/, "").toUpperCase()
        case "size":    return marked("size", sizePct + "%")
        case "opacity": return marked("opacity", opacityPct + "%")
        case "offsetx": return marked("offset_x", offsetText(offsetX))
        case "offsety": return marked("offset_y", offsetText(offsetY))
        }
        return ""
    }

    function helpFor(i) {
        switch (rows[i]) {
        case "logo":    return perChannel
                            ? "The mark this channel flies. DEFAULT is the module's default logo."
                            : "Used by channels that have not chosen one. Drawing one costs about a third of decode speed."
        case "size":    return "How much of the picture the logo covers, whatever its shape."
        case "opacity": return "Solid at 100%. A station bug is usually faint."
        case "offsetx": return "Nudge left or right. Increase if a CRT is cutting off the right edge."
        case "offsety": return "Nudge up or down. Increase if a CRT is cutting off the top."
        case "follow":  return "Forget this channel's own size, opacity and position and use the module's."
        case "preview": return "See it drawn on a blank screen, exactly as it will air."
        }
        return ""
    }

    function cycles(i) { return rows[i] !== "logo" && rows[i] !== "preview" && rows[i] !== "follow" }

    function step(delta) {
        switch (rows[current]) {
        case "size":
            save("size", Math.max(4, Math.min(33, sizePct + delta)))
            break
        case "opacity":
            save("opacity", Math.max(10, Math.min(100, opacityPct + delta * 5)))
            break
        case "offsetx":
            save("offset_x", Math.max(-20, Math.min(20, offsetX + delta)))
            break
        case "offsety":
            save("offset_y", Math.max(-20, Math.min(20, offsetY + delta)))
            break
        }
    }

    function open(i) {
        if (rows[i] === "logo") {
            var params = { moduleId: logoRoot.moduleId, settingKey: "logo.file" }
            if (perChannel) { params.channelNumber = channelNumber; params.title = heading }
            navigateTo("modules/virtual_channels/views/LogoPicker.qml", params,
                       { currentIndex: logoRoot.current })
        } else if (rows[i] === "follow") {
            virtualChannelsBackend.clear_channel_logo_style(channelNumber)
            status = "Following Channels settings"
            reload()
        } else if (rows[i] === "preview") {
            var p = { moduleId: logoRoot.moduleId }
            if (perChannel) p.channelNumber = channelNumber
            navigateTo("modules/virtual_channels/views/LogoPreview.qml", p,
                       { currentIndex: logoRoot.current })
        }
    }

    Component.onCompleted: {
        reload()
        if (navListState.currentIndex !== undefined)
            current = Math.min(navListState.currentIndex, rowCount - 1)
    }

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: logoRoot.moduleIcon
        title: logoRoot.perChannel ? logoRoot.heading + " Logo" : "Channel Logo"
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
