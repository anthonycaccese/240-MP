import QtQuick
import Components

// Channel list for a group. Selecting a channel hands its stream URL to the
// live player.
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

    Component.onCompleted: iptvBackend.load_channels(group)

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
        height: root.sh * 0.525
        clip: true
        focus: true

        Keys.onUpPressed: if (currentIndex > 0) currentIndex--
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++

        Keys.onReturnPressed: {
            var ch = channels[currentIndex]
            if (!ch || !ch.url) return
            channelsRoot.navigateTo("Player.qml", {
                streamUrl: ch.url,
                title: ch.name
            }, { currentIndex: channelList.currentIndex })
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                channelsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: channelList.width
            height: root.sh * 0.0583333

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, channelList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: channelList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.name || modelData.url || ""
                    color: channelList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    rightPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.05
                }

                SequentialAnimation {
                    running: (channelList.currentIndex === index) &&
                             (rowText.implicitWidth > textClip.width)
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) rowText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: rowText; property: "x"
                        to: textClip.width - rowText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: rowText; property: "x"; value: 0 }
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
