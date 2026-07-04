import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    // The subscriptions file and the watch-later/history files are local reads,
    // so this menu builds synchronously — isLoading exists only for pattern
    // parity with the other modules.
    Component.onCompleted: {
        var status = youtubeBackend.check_subscriptions()
        if (!status.ok) {
            errorMessage = status.error
            return
        }
        var list = ["Subscriptions", "Channels"]
        if (youtubeBackend.getWatchLater().length > 0)
            list.push("Watch Later")
        if (youtubeBackend.getHistory().length > 0)
            list.push("History")
        items = list
        var restore = navListState.currentIndex !== undefined ? navListState.currentIndex : 0
        itemList.currentIndex = Math.min(restore, Math.max(0, itemList.count - 1))
        itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
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
    }

    // Loading / Error states
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
        width: root.sw * 0.76875 //492 — long guidance lines wrap instead of clipping offscreen
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }

    // List
    ListView {
        id: itemList
        model: itemsRoot.items
        visible: !isLoading && errorMessage === ""
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
            height: root.sh * 0.0583333

            Rectangle {
                color: root.accentColor
                anchors.fill: label
                visible: itemList.currentIndex === index
            }

            Text {
                id: label
                text: modelData
                color: itemList.currentIndex === index ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.05
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: root.sw * 0.009375
                rightPadding: root.sw * 0.009375
                topPadding: root.sh * 0.0041667
                bottomPadding: root.sh * 0.00625
            }
        }

        Keys.onReturnPressed: {
            var selected = itemsRoot.items[itemList.currentIndex]
            if (!selected)
                return
            var state = { currentIndex: itemList.currentIndex }
            if (selected === "Subscriptions")
                navigateTo("Subscriptions.qml", { mode: "feed" }, state)
            else if (selected === "Channels")
                navigateTo("Channels.qml", {}, state)
            else if (selected === "Watch Later")
                navigateTo("Subscriptions.qml", { mode: "watchlater" }, state)
            else if (selected === "History")
                navigateTo("Subscriptions.qml", { mode: "history" }, state)
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
