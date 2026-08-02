import QtQuick
import Components

// Live TV channel list. Lists the tunable channels of the server's DVR; selecting
// one hands off to LivePlayer.qml, which tunes it and can switch channels in place.
FocusScope {
    id: channelsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string libraryName: navParams.libraryName || "LIVE TV"
    property var channels: []
    property bool loaded: false
    property string errorMessage: ""

    Connections {
        target: plexBackend

        function onLiveChannelsLoaded(items) {
            channelsRoot.channels = items
            channelsRoot.loaded = true
            if (items.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                channelList.currentIndex = Math.min(restore, items.length - 1)
                channelList.positionViewAtIndex(channelList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            console.log("[LiveChannels] Error: " + msg)
            channelsRoot.loaded = true
            channelsRoot.errorMessage = msg
        }
    }

    Component.onCompleted: plexBackend.load_live_channels()

    focus: true

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: channelsRoot.libraryName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Loading / empty / error indicator
    Text {
        visible: channels.length === 0
        text: channelsRoot.errorMessage !== "" ? channelsRoot.errorMessage
              : (channelsRoot.loaded ? "NO CHANNELS" : "LOADING...")
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        width: root.sw * 0.6
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    ListView {
        id: channelList
        model: channels
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

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
            if (channels.length === 0) return
            channelsRoot.navigateTo("LivePlayer.qml", {
                channel: channels[currentIndex]
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
            height: root.sh * 0.0583333 //28

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
                    text: (modelData.number ? modelData.number + "  " : "") + (modelData.title || "")
                    color: channelList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
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

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":WATCH"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
