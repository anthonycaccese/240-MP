import QtQuick
import Components

FocusScope {
    id: moduleSettingsRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})
    property string moduleId: navParams.moduleId || ""
    property string moduleName: ""

    property var schemaItems: []    // filtered schema entries from JSON
    property var currentValues: ({}) // config.modules[moduleId]
    property var dynamicOptions: ({}) // key -> [{id, label}] loaded from backend
    property string authState: ""

    function loadSettings() {
        var allSettings = appCore.get_settings()
        currentValues = (allSettings.modules && allSettings.modules[moduleId]) ? allSettings.modules[moduleId] : {}

        var mods = appCore.get_installed_modules()
        for (var i = 0; i < mods.length; i++) {
            if (mods[i].id === moduleId) { moduleName = mods[i].name; break }
        }

        buildModel()
    }

    function buildModel() {
        var schema = appCore.get_module_settings_schema(moduleId)
        authState = appCore.get_module_auth_state(moduleId)
        var filtered = []
        for (var i = 0; i < schema.length; i++) {
            var item = schema[i]
            if (item.requires_auth && (authState === "none" || authState === "")) continue
            filtered.push(item)
        }
        schemaItems = filtered

        if (filtered.length > 0) {
            var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
            settingsList.currentIndex = Math.min(restore, filtered.length - 1)
            settingsList.positionViewAtIndex(settingsList.currentIndex, ListView.Contain)
        }
    }

    function currentDisplayValue(item) {
        var key = item.key
        var type = item.type

        if (type === "toggle") {
            var raw = currentValues[key]
            if (raw === undefined || raw === null) raw = (item.default === "ON")
            return (raw === true || raw === "ON") ? "ON" : "OFF"
        }

        if (type === "list_single") {
            if (item.options_source === "dynamic") {
                var opts = dynamicOptions[key] || []
                var storedId = currentValues[key] || null
                for (var i = 0; i < opts.length; i++) {
                    if (opts[i].id === storedId) return opts[i].label
                }
                return opts.length > 0 ? opts[0].label : "---"
            }
            return currentValues[key] || (item.default || "---")
        }

        if (type === "directory_browser") {
            var saved = currentValues[key] || ""
            return saved !== "" ? saved : "Default"
        }

        return ""
    }

    function cycleValue(index, direction) {
        var item = schemaItems[index]
        if (!item) return

        if (item.type === "toggle") {
            var cur = currentDisplayValue(item)
            var newVal = (cur === "ON") ? false : true
            var updated = Object.assign({}, currentValues)
            updated[item.key] = newVal
            currentValues = updated
            appCore.save_setting(moduleId, item.key, newVal)
            settingsList.forceLayout()
        } else if (item.type === "list_single") {
            var opts
            if (item.options_source === "dynamic") {
                opts = dynamicOptions[item.key] || []
                if (opts.length === 0) return
                var storedId = currentValues[item.key] || null
                var curIdx = 0
                for (var i = 0; i < opts.length; i++) {
                    if (opts[i].id === storedId) { curIdx = i; break }
                }
                var newIdx = (curIdx + direction + opts.length) % opts.length
                var newId = opts[newIdx].id
                var u = Object.assign({}, currentValues)
                u[item.key] = newId
                currentValues = u
                appCore.save_setting(moduleId, item.key, newId)
                if (item.apply_slot) {
                    appCore.invoke_module_action(moduleId, item.apply_slot)
                }
            } else {
                opts = item.options || []
                if (opts.length === 0) return
                var curVal = currentValues[item.key] || opts[0]
                var ci = opts.indexOf(curVal)
                if (ci < 0) ci = 0
                var ni = (ci + direction + opts.length) % opts.length
                var u2 = Object.assign({}, currentValues)
                u2[item.key] = opts[ni]
                currentValues = u2
                appCore.save_setting(moduleId, item.key, opts[ni])
            }
            settingsList.forceLayout()
        }
    }

    // Forward dynamicOptionsReady from appCore into our local cache
    Connections {
        target: appCore
        function onDynamicOptionsReady(mid, key, items) {
            if (mid !== moduleSettingsRoot.moduleId) return
            var updated = Object.assign({}, moduleSettingsRoot.dynamicOptions)
            updated[key] = items
            moduleSettingsRoot.dynamicOptions = updated
            settingsList.forceLayout()
        }
        function onModuleAuthStateChanged(mid) {
            if (mid !== moduleSettingsRoot.moduleId) return
            buildModel()
            for (var i = 0; i < schemaItems.length; i++) {
                var item = schemaItems[i]
                if (item.options_source === "dynamic" && item.options_slot) {
                    appCore.invoke_module_action(moduleId, item.options_slot)
                }
            }
        }
    }

    Component.onCompleted: {
        loadSettings()
        // Kick off loading dynamic option lists for any dynamic settings
        for (var i = 0; i < schemaItems.length; i++) {
            var item = schemaItems[i]
            if (item.options_source === "dynamic" && item.options_slot) {
                appCore.invoke_module_action(moduleId, item.options_slot)
            }
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    ListView {
        id: settingsList
        model: schemaItems
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
            moduleSettingsRoot.cycleValue(currentIndex, -1)
        }
        Keys.onRightPressed: {
            moduleSettingsRoot.cycleValue(currentIndex, 1)
        }

        Keys.onReturnPressed: {
            var item = schemaItems[currentIndex]
            if (!item) return
            if (item.type === "toggle") {
                moduleSettingsRoot.cycleValue(currentIndex, 1)
            } else if (item.type === "multiselect_submenu") {
                moduleSettingsRoot.navigateTo("views/MultiSelectSettings.qml", {
                    moduleId: moduleSettingsRoot.moduleId,
                    settingKey: item.key,
                    settingLabel: item.label
                }, { currentIndex: settingsList.currentIndex })
            } else if (item.type === "action") {
                appCore.invoke_module_action(moduleSettingsRoot.moduleId, item.action_slot)
            } else if (item.type === "directory_browser") {
                var savedPath = currentValues[item.key] || ""
                moduleSettingsRoot.navigateTo("views/DirectoryBrowser.qml", {
                    moduleId: moduleSettingsRoot.moduleId,
                    settingKey: item.key,
                    currentPath: savedPath !== "" ? savedPath : appCore.homePath()
                }, { currentIndex: settingsList.currentIndex })
            }
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                moduleSettingsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: settingsList.width
            height: root.sh * 0.0583333 //28

            Rectangle {
                anchors.fill: parent
                color: settingsList.currentIndex === index ? root.accentColor : "transparent"

                // Label
                Text {
                    text: modelData.label || ""
                    color: settingsList.currentIndex === index ? root.surfaceColor : root.primaryColor
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

                // Right-side value/arrow
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: root.sw * 0.009375 //6
                    spacing: root.sw * 0.00625 //4

                    // Left arrow for cycled types
                    Text {
                        visible: modelData.type === "toggle" || modelData.type === "list_single"
                        text: "\u25C4"
                        color: settingsList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
                        font.family: root.globalFont
                        anchors.verticalCenter: parent.verticalCenter
                        topPadding: root.sh * 0.0041667 //2
                        bottomPadding: root.sh * 0.00625 //3
                        font.pixelSize: root.sh * 0.0375 //18
                    }

                    // Current value text (cycled types + directory_browser) with marquee scroll
                    Item {
                        id: valueClip
                        visible: modelData.type === "toggle" || modelData.type === "list_single" || modelData.type === "directory_browser"
                        width: Math.min(valueText.implicitWidth, root.sw * 0.35)
                        height: parent.height
                        clip: true

                        Text {
                            id: valueText
                            text: moduleSettingsRoot.currentDisplayValue(modelData)
                            color: settingsList.currentIndex === index ? root.surfaceColor : root.primaryColor
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
                            running: settingsList.currentIndex === index && valueText.implicitWidth > valueClip.width
                            loops: Animation.Infinite
                            onRunningChanged: if (!running) valueText.x = 0

                            PauseAnimation { duration: 1500 }
                            NumberAnimation {
                                target: valueText
                                property: "x"
                                to: valueClip.width - valueText.implicitWidth
                                duration: Math.abs(to) * 20
                            }
                            PauseAnimation { duration: 2000 }
                            PropertyAction { target: valueText; property: "x"; value: 0 }
                        }
                    }

                    // Right arrow (always shown)
                    Text {
                        text: "\u25BA"
                        color: settingsList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
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
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.change + ":CHANGE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
