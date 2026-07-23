import QtQuick
import Components

// Channel list for a group, with now/next guide info (when an XMLTV EPG is
// configured). Selecting a channel hands its stream URL to the live player.
FocusScope {
    id: channelsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    property string group: navParams.group || "__all__"
    property string title: navParams.title || "CHANNELS"

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var channels: []
    property bool loading: true

    // Wall-clock in epoch-ms, ticked so the "now" progress bars advance while the
    // list is on screen.
    property double nowMs: 0

    Timer {
        interval: 30000
        repeat: true
        running: true
        onTriggered: channelsRoot.nowMs = Date.now()
    }

    function progressFor(item) {
        var start = item.nowStartMs || 0
        var stop = item.nowStopMs || 0
        if (stop <= start) return -1
        var f = (channelsRoot.nowMs - start) / (stop - start)
        return Math.max(0, Math.min(1, f))
    }

    Connections {
        target: iptvBackend

        function onChannelsLoaded(items) {
            channelsRoot.channels = items
            channelsRoot.loading = false
            var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
            channelList.currentIndex = Math.min(restore, Math.max(0, items.length - 1))
            channelList.positionViewAtIndex(channelList.currentIndex, ListView.Contain)
        }
    }

    Component.onCompleted: {
        nowMs = Date.now()
        iptvBackend.load_channels(group)
    }

    focus: true

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: channelsRoot.title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Text {
        visible: loading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }

    Text {
        visible: !loading && channels.length === 0
        text: "NO CHANNELS"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }

    ListView {
        id: channelList
        model: channels
        visible: !loading && channels.length > 0
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.55
        clip: true
        focus: true
        spacing: root.sh * 0.0083333

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            channelList.positionViewAtIndex(channelList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            channelList.positionViewAtIndex(channelList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            var ch = channels[currentIndex]
            if (!ch || !ch.url) return
            channelsRoot.navigateTo("Player.qml", {
                streamUrl: ch.url,
                title: ch.name,
                nowTitle: ch.nowTitle || "",
                nextTitle: ch.nextTitle || ""
            }, { currentIndex: channelList.currentIndex })
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                channelsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            id: chRow
            width: channelList.width
            height: nowLine.visible ? root.sh * 0.09 : root.sh * 0.0583333
            property bool selected: channelList.currentIndex === index

            Rectangle {
                anchors.fill: parent
                color: root.accentColor
                visible: chRow.selected
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: root.sw * 0.009375
                anchors.rightMargin: root.sw * 0.009375
                spacing: root.sh * 0.004

                Text {
                    text: modelData.name || modelData.url || ""
                    color: chRow.selected ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0416667
                    elide: Text.ElideRight
                    width: parent.width
                }

                Text {
                    id: nowLine
                    visible: (modelData.nowTitle || modelData.nextTitle || "") !== ""
                    text: modelData.nowTitle
                          ? "NOW · " + modelData.nowTitle
                          : (modelData.nextTitle ? "NEXT · " + modelData.nextTitle : "")
                    color: chRow.selected ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0270833
                    elide: Text.ElideRight
                    width: parent.width
                }

                // Progress through the current programme.
                Rectangle {
                    visible: channelsRoot.progressFor(modelData) >= 0
                    width: parent.width
                    height: root.sh * 0.005
                    color: chRow.selected ? Qt.rgba(1,1,1,0.35) : root.surfaceColor

                    Rectangle {
                        height: parent.height
                        width: parent.width * Math.max(0, channelsRoot.progressFor(modelData))
                        color: chRow.selected ? root.surfaceColor : root.accentColor
                    }
                }
            }
        }
    }

    Text {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":WATCH"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
