import QtQuick
import Components

FocusScope {
    id: manageRoot
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property string moduleId: navParams.moduleId || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var channels: []
    property string status: ""

    readonly property var listRows: {
        var r = []
        for (var i = 0; i < channels.length; i++) r.push(channels[i])
        r.push({ addRow: true, number: "+", name: "Add Channel" })
        return r
    }
    function isAddRow(i) { return i === channels.length }

    focus: true

    property var navListState: navParams.navListState || ({})

    function refresh() {
        channels = virtualChannelsBackend.list_channels()
        if (channelList.currentIndex >= channels.length)
            channelList.currentIndex = Math.max(0, channels.length - 1)
    }

    function selected() {
        var i = channelList.currentIndex
        return (i >= 0 && i < channels.length) ? channels[i] : null
    }

    function applyPendingCreate() {
        if (!navListState.createChannel) return
        var typed = appCore.get_setting(moduleId, "create_buffer")
        if (typed && String(typed).trim() !== "") {
            var n = virtualChannelsBackend.create_channel(String(typed))
            status = n >= 0 ? "Created channel " + n + " — now choose its source"
                            : "Could not create the channel"
        }
        appCore.save_setting(moduleId, "create_buffer", "")
    }

    function move(direction) {
        var c = selected()
        if (!c) return
        if (virtualChannelsBackend.is_generating()) {
            status = "A channel is still building — try again in a moment"
            return
        }
        if (virtualChannelsBackend.move_channel(c.number, direction)) {
            channelList.currentIndex = Math.max(0, Math.min(channels.length - 1,
                                                channelList.currentIndex + direction))
            status = ""
            refresh()
        } else {
            status = direction < 0 ? "Already first" : "Already last"
        }
    }

    function isBuiltIn(c) { return !!(c && c.special && c.special !== "") }

    Component.onCompleted: {
        applyPendingCreate()
        refresh()
        if (navListState.currentIndex !== undefined)
            channelList.currentIndex = Math.min(navListState.currentIndex,
                                                Math.max(0, listRows.length - 1))
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
        } else if (event.key === Qt.Key_Up) {
            channelList.currentIndex =
                (channelList.currentIndex - 1 + listRows.length) % listRows.length
        } else if (event.key === Qt.Key_Down) {
            channelList.currentIndex = (channelList.currentIndex + 1) % listRows.length
        } else if (event.key === Qt.Key_Left) {
            move(-1)
        } else if (event.key === Qt.Key_Right) {
            move(1)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (isAddRow(channelList.currentIndex)) {
                appCore.save_setting(moduleId, "create_buffer", "")
                navigateTo("modules/virtual_channels/views/TextEntry.qml", {
                    moduleId: manageRoot.moduleId,
                    settingKey: "create_buffer",
                    title: "Name The New Channel",
                    initialText: ""
                }, { currentIndex: channelList.currentIndex, createChannel: true })
            } else {
                var c = selected()
                if (!c) {
                } else if (isBuiltIn(c)) {
                    status = "Built-in — use " + root.hints.change + " to move it"
                } else {
                    navigateTo("modules/virtual_channels/views/ChannelSources.qml", {
                        moduleId:      manageRoot.moduleId,
                        channelNumber: c.number,
                        channelName: c.name
                    }, { currentIndex: channelList.currentIndex })
                }
            }
        }
        event.accepted = true
    }

    AppBar {
        id: appBar
        iconSource: manageRoot.moduleIcon
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.09
        anchors.leftMargin: root.sw * 0.125
        title: "Manage Channels"
    }

    Text {
        anchors.centerIn: parent
        visible: false
        text: ""
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0333333
    }

    ListView {
        id: channelList
        anchors.top: appBar.bottom
        anchors.topMargin: root.sh * 0.03
        anchors.bottom: statusText.top
        anchors.bottomMargin: root.sh * 0.02
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75
        clip: true
        model: manageRoot.listRows
        currentIndex: 0
        highlightMoveDuration: 0
        interactive: false

        delegate: Item {
            id: chRow
            width: channelList.width
            height: root.sh * 0.07

            required property int index
            required property var modelData
            readonly property bool selected: index === channelList.currentIndex
            readonly property bool isAdd: modelData.addRow === true

            Rectangle {
                anchors.fill: parent
                color: chRow.selected ? root.accentColor : "transparent"
            }

            Text {
                id: num
                text: chRow.modelData.number
                color: chRow.selected ? root.surfaceColor : root.tertiaryColor
                font.family: root.globalFont
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.009375
                width: root.sw * 0.05
                horizontalAlignment: Text.AlignRight
                font.pixelSize: root.sh * 0.0375
            }

            Text {
                text: chRow.modelData.name
                color: chRow.selected ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: num.right
                anchors.leftMargin: root.sw * 0.01875
                width: root.sw * 0.40
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.0375
            }

            Text {
                visible: !chRow.isAdd
                text: chRow.modelData.special && chRow.modelData.special !== ""
                      ? "BUILT-IN"
                      : String(chRow.modelData.source || "local").toUpperCase()
                        + (chRow.modelData.hasSchedule ? "" : " · NOT BUILT")
                color: chRow.selected ? root.surfaceColor : root.tertiaryColor
                font.family: root.globalFont
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.009375
                font.pixelSize: root.sh * 0.0270833
            }
        }
    }

    Text {
        id: statusText
        visible: manageRoot.status !== ""
        text: manageRoot.status
        color: root.secondaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.bottom: footer.top
        anchors.bottomMargin: root.sh * 0.0208333
        anchors.left: parent.left
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
    }

    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":SELECT "
              + (manageRoot.isAddRow(channelList.currentIndex)
                 ? "" : root.hints.change + ":MOVE ")
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
