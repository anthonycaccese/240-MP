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
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        }
    }

    // TODO: replace with real data source
    property var items: ["Item A", "Item B", "Item C", "Item D", "Item E"]

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
            var selected = itemsRoot.items[itemList.currentIndex]
            navigateTo("Detail.qml", { item: selected }, { currentIndex: itemList.currentIndex })
        }
    }

    Text {
        id: footer
        text: "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
