import QtQuick
import Components

FocusScope {
    id: pickerRoot

    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: ({})
    property string moduleId: navParams.moduleId || ""
    property string settingKey: navParams.settingKey || "logo.file"
    property int    channelNumber: navParams.channelNumber !== undefined
                                   ? navParams.channelNumber : -1
    readonly property bool perChannel: channelNumber >= 0
    property string heading: navParams.title || ""
    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var logos: []
    property string chosen: ""
    property string logosDir: ""
    property string defaultName: ""

    readonly property int rowCount: logos.length + 1
    property int current: 0

    focus: true

    function reload() {
        logosDir = virtualChannelsBackend.logos_dir()
        logos = virtualChannelsBackend.list_logos()
        chosen = perChannel ? (virtualChannelsBackend.channel_logo(channelNumber) || "")
                            : (appCore.get_setting(moduleId, settingKey) || "")
        if (perChannel) {
            var def = String(appCore.get_setting(moduleId, settingKey) || "")
            defaultName = ""
            for (var i = 0; i < logos.length; ++i)
                if (logos[i].file === def) { defaultName = String(logos[i].name); break }
        }
        if (current >= rowCount) current = rowCount - 1
    }

    function helpText() {
        if (logos.length === 0) return "Drop PNG or GIF files into " + logosDir
        if (current === 0 && perChannel)
            return defaultName === ""
                   ? "Follows the module's logo setting, which is currently no logo."
                   : "Follows the module's logo setting, currently "
                     + defaultName.toUpperCase() + "."
        if (current === 0) return "No logo. Nothing drawn over the picture, and nothing spent drawing it."
        var f = logos[current - 1]
        var name = f ? String(f.file) : ""
        if (name.toLowerCase().endsWith(".gif"))
            return "Moves, and costs most — about a third of decode speed, and more again joining part way in."
        return "A still logo, the cheaper of the two. Any logo costs roughly a third of decode speed."
    }

    function isOn(i) {
        if (i === 0) return chosen === ""
        return logos[i - 1].file === chosen
    }

    function pick(i) {
        var file = (i === 0) ? "" : logos[i - 1].file
        if (perChannel) virtualChannelsBackend.set_channel_logo(channelNumber, file)
        else            appCore.save_setting(moduleId, settingKey, file)
        reload()
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
            pick(current)
        }
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: root.surfaceColor }

    AppBar {
        id: appBar
        iconSource: pickerRoot.moduleIcon
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.075
        anchors.leftMargin: root.sw * 0.125
        title: pickerRoot.heading !== "" ? pickerRoot.heading + " — Logo" : "Default Logo"
    }

    ListView {
        id: list
        anchors.top: appBar.bottom
        anchors.topMargin: root.sh * 0.025
        anchors.bottom: helpBackground.top
        anchors.bottomMargin: root.sh * 0.02
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75
        clip: true
        interactive: false
        model: pickerRoot.rowCount
        currentIndex: pickerRoot.current
        highlightMoveDuration: 0

        delegate: Item {
            required property int index
            width: list.width
            height: root.sh * 0.06
            readonly property bool selected: index === pickerRoot.current
            readonly property bool on: pickerRoot.isOn(index)

            Rectangle {
                anchors.fill: parent
                color: parent.selected ? root.accentColor : "transparent"
            }

            Rectangle {
                id: box
                width: root.sh * 0.028
                height: width
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0125
                color: parent.on ? (parent.selected ? root.surfaceColor : root.primaryColor)
                                 : "transparent"
                border.color: parent.selected ? root.surfaceColor : root.tertiaryColor
                border.width: 2
            }

            Text {
                text: parent.index === 0
                      ? (pickerRoot.perChannel ? "Use The Default" : "No Logo")
                      : pickerRoot.logos[parent.index - 1].name
                color: parent.selected ? root.surfaceColor
                                       : (parent.on ? root.primaryColor : root.tertiaryColor)
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: box.right
                anchors.leftMargin: root.sw * 0.0187
                anchors.right: tag.left
                anchors.rightMargin: root.sw * 0.0125
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.0333
            }

            Text {
                id: tag
                text: parent.index > 0 && pickerRoot.logos[parent.index - 1].animated ? "ANIMATED" : ""
                color: parent.selected ? root.surfaceColor : root.tertiaryColor
                font.family: root.globalFont
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.0125
                font.pixelSize: root.sh * 0.0250
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
            text: pickerRoot.helpText()
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
              + root.hints.select + ":CHOOSE"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.0833333
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
    }
}
