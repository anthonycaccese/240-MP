import QtQuick
import Components

// Video list — serves both the aggregated Subscriptions feed (mode "feed")
// and a single channel's videos (mode "channel", via channelId/channelName).
FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string mode: navParams.mode || "feed"
    property string channelId: navParams.channelId || ""
    property string channelName: navParams.channelName || ""

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    Component.onCompleted: {
        isLoading = true
        errorMessage = ""
        if (mode === "feed")
            youtubeBackend.load_subscriptions_feed()
        else
            youtubeBackend.load_channel_videos(channelId)
    }

    Connections {
        target: youtubeBackend

        function onSubscriptionsFeedLoaded(videos) {
            if (itemsRoot.mode !== "feed")
                return
            itemsRoot.isLoading = false
            itemsRoot.items = videos
            if (videos.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                itemList.currentIndex = Math.min(restore, videos.length - 1)
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
            }
        }

        function onChannelVideosLoaded(loadedChannelId, videos) {
            if (itemsRoot.mode !== "channel" || loadedChannelId !== itemsRoot.channelId)
                return
            itemsRoot.isLoading = false
            itemsRoot.items = videos
            if (videos.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                itemList.currentIndex = Math.min(restore, videos.length - 1)
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            itemsRoot.isLoading = false
            itemsRoot.errorMessage = msg
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: itemsRoot.mode === "feed" ? "Subscriptions" : itemsRoot.channelName
    }

    // Loading / empty / error states
    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage !== ""
        text: errorMessage
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage === "" && items.length === 0
        text: "NO VIDEOS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // List
    ListView {
        id: itemList
        model: itemsRoot.items
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: true

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.075 //36

            // Full-width background highlight for the active row
            Rectangle {
                color: root.accentColor
                anchors.fill: parent
                visible: itemList.currentIndex === index
            }

            // LEFT SIDE: Vertical stack for Subtitle and Title
            Column {
                id: textColumn
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0109375 //7
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.sh * 0.0041667 //2

                Text {
                    id: subtitleLabel
                    text: modelData.channelName || ""
                    color: itemList.currentIndex === index ? root.surfaceColor : root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0208333 //10
                }

                Text {
                    id: titleLabel
                    text: modelData.title || ""
                    color: itemList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0333333 //16
                }
            }

            // RIGHT SIDE: Duration (only visible when highlighted)
            Text {
                id: durationLabel
                visible: itemList.currentIndex === index && text !== ""
                text: modelData.duration || ""
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.015625 //10
                anchors.verticalCenter: parent.verticalCenter
                color: root.surfaceColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.025 //12
            }
        }

        Keys.onReturnPressed: {
            var selected = itemsRoot.items[itemList.currentIndex]
            if (!selected)
                return
            navigateTo("Player.qml", { item: selected }, { currentIndex: itemList.currentIndex })
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
