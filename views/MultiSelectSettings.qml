import QtQuick
import Components

FocusScope {
    id: multiSelectRoot

    signal goBack()

    property var navParams: ({})
    property string moduleId: navParams.moduleId || ""
    property string settingKey: navParams.settingKey || ""
    property string settingLabel: navParams.settingLabel || ""

    property var options: []         // [{id, label, enabled}]
    property var enabledMap: ({})    // id -> bool

    function buildEnabledMap() {
        var allSettings = appCore.get_settings()
        var moduleConfig = (allSettings.modules && allSettings.modules[moduleId]) ? allSettings.modules[moduleId] : {}
        var storedMap = moduleConfig[settingKey] || {}
        var map = {}
        for (var i = 0; i < options.length; i++) {
            var id = options[i].id
            map[id] = (storedMap[id] !== undefined) ? storedMap[id] : true
        }
        enabledMap = map
    }

    function toggleItem(id) {
        var updated = Object.assign({}, enabledMap)
        updated[id] = !updated[id]
        enabledMap = updated
        // Save as nested key: settingKey.{id}
        appCore.save_setting(moduleId, settingKey + "." + id, enabledMap[id])
        optionsList.forceLayout()
    }

    // Receive dynamic options from backend via appCore
    Connections {
        target: appCore
        function onDynamicOptionsReady(mid, key, items) {
            if (mid !== multiSelectRoot.moduleId || key !== multiSelectRoot.settingKey) return
            multiSelectRoot.options = items
            multiSelectRoot.buildEnabledMap()
        }
    }

    Component.onCompleted: {
        // Request the options list from the backend
        // Find the options_slot for this setting key
        var schema = appCore.get_module_settings_schema(moduleId)
        for (var i = 0; i < schema.length; i++) {
            if (schema[i].key === settingKey && schema[i].options_slot) {
                appCore.invoke_module_action(moduleId, schema[i].options_slot)
                break
            }
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: moduleId.split(".").pop().toUpperCase() + " / " + settingLabel
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    ListView {
        id: optionsList
        model: options
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (currentIndex > 0) currentIndex--
        }
        Keys.onDownPressed: {
            if (currentIndex < count - 1) currentIndex++
        }

        Keys.onLeftPressed: {
            if (options[currentIndex]) toggleItem(options[currentIndex].id)
        }
        Keys.onRightPressed: {
            if (options[currentIndex]) toggleItem(options[currentIndex].id)
        }

        Keys.onReturnPressed: {
            if (options[currentIndex]) toggleItem(options[currentIndex].id)
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                multiSelectRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: optionsList.width
            height: root.sh * 0.0583333 //28

            Rectangle {
                anchors.fill: parent
                color: optionsList.currentIndex === index ? root.accentColor : "transparent"

                Text {
                    text: modelData.label || ""
                    color: optionsList.currentIndex === index ? root.surfaceColor : root.primaryColor
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

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: root.sw * 0.009375 //6
                    spacing: root.sw * 0.00625 //4

                    Text {
                        text: "\u25C4"
                        color: optionsList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
                        font.family: root.globalFont
                        anchors.verticalCenter: parent.verticalCenter
                        topPadding: root.sh * 0.0041667 //2
                        bottomPadding: root.sh * 0.00625 //3
                        font.pixelSize: root.sh * 0.0375 //18
                    }
                    Text {
                        text: multiSelectRoot.enabledMap[modelData.id] ? "ON" : "OFF"
                        color: optionsList.currentIndex === index ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        anchors.verticalCenter: parent.verticalCenter
                        topPadding: root.sh * 0.0041667 //2
                        leftPadding: root.sw * 0.009375 //6
                        rightPadding: root.sw * 0.009375 //6
                        bottomPadding: root.sh * 0.00625 //3
                        font.pixelSize:root.sh * 0.05 //24
                    }
                    Text {
                        text: "\u25BA"
                        color: optionsList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
                        font.family: root.globalFont
                        anchors.verticalCenter: parent.verticalCenter
                        topPadding: root.sh * 0.0041667 //2
                        bottomPadding: root.sh * 0.00625 //3
                        font.pixelSize: root.sh * 0.0375 //18
                    }
                }
            }
        }
    }

    // --- FOOTER ---
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.change + ":CHANGE"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
