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

    property var items: ["Subscriptions", "Channels"]
    property bool isLoading: false
    property string errorMessage: ""

    // The subscriptions file is a local read, so the check is synchronous —
    // isLoading exists only for pattern parity with the other modules.
    Component.onCompleted: {
        var status = youtubeBackend.check_subscriptions()
        if (!status.ok)
            errorMessage = status.error
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

        Component.onCompleted: {
            var restore = navListState.currentIndex !== undefined ? navListState.currentIndex : 0
            currentIndex = Math.min(restore, Math.max(0, count - 1))
            positionViewAtIndex(currentIndex, ListView.Contain)
        }

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
            if (errorMessage !== "")
                return
            if (itemList.currentIndex === 0)
                navigateTo("Subscriptions.qml", { mode: "feed" }, { currentIndex: itemList.currentIndex })
            else
                navigateTo("Channels.qml", {}, { currentIndex: itemList.currentIndex })
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
