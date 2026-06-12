import QtQuick
import Components

FocusScope {
    id: browserRoot

    signal goBack()

    property var navParams: ({})
    property string moduleId: navParams.moduleId || ""
    property string settingKey: navParams.settingKey || ""

    property string currentBrowsePath: ""
    property var dirEntries: []

    property var listModel: {
        var items = [
            { name: "..PARENT DIRECTORY", entryType: "up" },
            { name: "<USE THIS DIRECTORY>", entryType: "select" }
        ]
        for (var i = 0; i < dirEntries.length; i++) {
            items.push({ name: dirEntries[i].name, path: dirEntries[i].path, entryType: "dir" })
        }
        return items
    }

    function loadEntries() {
        dirEntries = appCore.listDirectories(currentBrowsePath)
        dirList.currentIndex = 0
    }

    function navigateInto(path) {
        currentBrowsePath = path
        loadEntries()
    }

    function goUp() {
        var parent = appCore.parentDirectory(currentBrowsePath)
        if (parent === currentBrowsePath) return
        currentBrowsePath = parent
        loadEntries()
    }

    function selectCurrent() {
        appCore.save_setting(moduleId, settingKey, currentBrowsePath)
        goBack()
    }

    Component.onCompleted: {
        currentBrowsePath = navParams.currentPath || appCore.homePath()
        loadEntries()
    }

    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: currentBrowsePath
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    ListView {
        id: dirList
        model: browserRoot.listModel
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (currentIndex > 0) currentIndex--
        }
        Keys.onDownPressed: {
            if (currentIndex < count - 1) currentIndex++
        }
        Keys.onReturnPressed: {
            var entry = browserRoot.listModel[currentIndex]
            if (!entry) return
            if (entry.entryType === "up") browserRoot.goUp()
            else if (entry.entryType === "select") browserRoot.selectCurrent()
            else if (entry.entryType === "dir") browserRoot.navigateInto(entry.path)
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                browserRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: dirList.width
            height: root.sh * 0.0583333

            Rectangle {
                anchors.fill: parent
                color: dirList.currentIndex === index ? root.accentColor : "transparent"

                Text {
                    text: modelData.name || ""
                    color: {
                        if (dirList.currentIndex === index) return root.surfaceColor
                        if (modelData.entryType === "up" || modelData.entryType === "select") return root.accentColor
                        return root.primaryColor
                    }
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    rightPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.05
                }
            }
        }
    }

    Text {
        text: root.hints.back + ":CANCEL  " + root.hints.navigate + ":NAVIGATE  " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
