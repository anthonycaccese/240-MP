import QtQuick

FocusScope {
    id: previewRoot

    property var navParams: ({})
    property var navListState: ({})
    property string moduleId: navParams.moduleId || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property int channelNumber: navParams.channelNumber !== undefined
                                ? navParams.channelNumber : -1
    readonly property real inset: 0.08
    property string file: ""
    property real sizePct: 12
    property real opacityPct: 70
    property int offsetX: 0
    property int offsetY: 0

    focus: true

    function reload() {
        file = channelNumber >= 0
               ? (virtualChannelsBackend.channel_logo(channelNumber) || "") : ""
        if (file === "") file = appCore.get_setting(moduleId, "logo.file") || ""
        var sz     = parseFloat(appCore.get_setting(moduleId, "logo.size"))
        var op     = parseFloat(appCore.get_setting(moduleId, "logo.opacity"))
        var ox     = parseInt(appCore.get_setting(moduleId, "logo.offset_x"))
        var oy     = parseInt(appCore.get_setting(moduleId, "logo.offset_y"))
        sizePct    = isNaN(sz) ? 12 : sz
        opacityPct = isNaN(op) ? 70 : op
        offsetX    = isNaN(ox) ? 0 : ox
        offsetY    = isNaN(oy) ? 0 : oy
        fullPath   = file === "" ? "" : virtualChannelsBackend.logo_path(file)
    }

    property string fullPath: ""

    Component.onCompleted: reload()

    Keys.onPressed: function(event) {
        goBack()
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: "black" }

    AnimatedImage {
        id: logo
        visible: previewRoot.fullPath !== ""
        source: previewRoot.fullPath
        playing: true
        cache: false
        fillMode: Image.PreserveAspectFit
        readonly property real aspect: sourceSize.width > 0
                                       ? sourceSize.height / sourceSize.width : 1
        width: previewRoot.width * (previewRoot.sizePct / 100.0)
        height: width * aspect
        opacity: previewRoot.opacityPct / 100.0
        x: previewRoot.width - width - previewRoot.width * previewRoot.inset
           + previewRoot.width * (previewRoot.offsetX / 100.0)
        y: previewRoot.height - height - previewRoot.height * previewRoot.inset
           + previewRoot.height * (previewRoot.offsetY / 100.0)
    }

    Text {
        anchors.centerIn: parent
        visible: previewRoot.fullPath === ""
        text: "NO LOGO CHOSEN"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0333
    }

    Text {
        text: "ANY KEY TO GO BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: root.sh * 0.0833333
        font.pixelSize: root.sh * 0.0291667
    }
}
