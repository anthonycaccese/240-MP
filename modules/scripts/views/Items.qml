import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    readonly property var currentScript: scriptList.model[scriptList.currentIndex] || null

    property bool confirmVisible: false
    property var  pendingScript: null

    focus: true

    // Scripts marked "confirm = yes" get a yes/no prompt first; everything else
    // launches straight away.
    function activate(entry) {
        if (!entry) return
        if (entry.confirm) {
            pendingScript = entry
            confirmVisible = true
        } else {
            launch(entry)
        }
    }

    // TODO(Phase 4): "takeover" scripts route to Takeover.qml, which brackets the
    // launch with the DisplayHandoff. Until then every script runs in console mode.
    function launch(entry) {
        navigateTo("Console.qml",
                   { basename: entry.basename, name: entry.name },
                   { currentIndex: scriptList.currentIndex })
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Empty state
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333 //16
        visible: scriptList.count === 0
        Text {
            text: "No scripts found"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.05 //24
        }
        Text {
            text: "Please add .sh files to the scripts directory"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    ListView {
        id: scriptList
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.4666667 //224
        keyNavigationEnabled: true
        clip: true
        focus: true

        Keys.onReturnPressed: itemsRoot.activate(scriptList.model[scriptList.currentIndex])

        delegate: Item {
            width: scriptList.width
            height: root.sh * 0.0583333 //28

            // The mode tag is laid out first so the name's clip region can stop
            // short of it — a long name must never run underneath the tag.
            Text {
                id: modeTag
                text: modelData.mode === "takeover" ? "TAKEOVER" : "CONSOLE"
                color: scriptList.currentIndex === index ? root.accentColor : root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.009375 //6
                font.pixelSize: root.sh * 0.0291667 //14
            }

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth,
                                scriptList.width - modeTag.width - root.sw * 0.028125)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: scriptList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.name
                    color: scriptList.currentIndex === index ? root.surfaceColor : root.primaryColor
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
                    running: (scriptList.currentIndex === index) &&
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

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            scriptList.positionViewAtIndex(scriptList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            scriptList.positionViewAtIndex(scriptList.currentIndex, ListView.Contain)
        }
    }

    // --- DETAIL BAR --- the selected script's file and sidecar flags, so what was
    // parsed out of the .txt is visible without leaving the app.
    Rectangle {
        id: detailBackground
        visible: !!itemsRoot.currentScript
        property color baseColor: root.primaryColor
        color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.2)
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1583333 //76
        anchors.leftMargin: root.sw * 0.125 //80
        width: root.sw * 0.75 //480
        height: root.sh * 0.0583333 //28
        clip: true

        Text {
            text: {
                var s = itemsRoot.currentScript
                if (!s) return ""
                var flags = []
                if (s.favorite) flags.push("FAVORITE")
                if (s.confirm)  flags.push("CONFIRM")
                return s.basename + (flags.length > 0 ? "  •  " + flags.join("  •  ") : "")
            }
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0291667 //14
            elide: Text.ElideMiddle
            anchors.fill: parent
            anchors.margins: root.sw * 0.0125 //6
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Component.onCompleted: {
        var loaded = scriptsBackend.getScripts()
        scriptList.model = loaded
        if (loaded.length > 0) {
            var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
            scriptList.currentIndex = Math.min(restore, loaded.length - 1)
            scriptList.positionViewAtIndex(scriptList.currentIndex, ListView.Contain)
        }
        scriptList.forceActiveFocus()
    }

    // --- RUN CONFIRMATION OVERLAY --- (sidecar "confirm = yes")
    Rectangle {
        id: confirmOverlay
        anchors.fill: parent
        color: root.surfaceColor
        visible: itemsRoot.confirmVisible
        focus: itemsRoot.confirmVisible
        // Above the list AND the footer, which is a later sibling and would
        // otherwise draw through the overlay.
        z: 100

        property int choiceIndex: 0
        // Cancel first so a stray ENTER can't run something destructive.
        readonly property var choices: [
            { label: "Cancel", run: false },
            { label: "Run",    run: true  }
        ]

        onVisibleChanged: if (visible) choiceIndex = 0

        function dismiss() {
            itemsRoot.confirmVisible = false
            itemsRoot.pendingScript = null
            scriptList.forceActiveFocus()
        }

        Keys.onUpPressed:   choiceIndex = Math.max(0, choiceIndex - 1)
        Keys.onDownPressed: choiceIndex = Math.min(choices.length - 1, choiceIndex + 1)
        Keys.onReturnPressed: {
            var entry = itemsRoot.pendingScript
            var doRun = choices[choiceIndex].run
            confirmOverlay.dismiss()
            if (doRun && entry) itemsRoot.launch(entry)
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                confirmOverlay.dismiss()
                event.accepted = true
            }
        }

        Rectangle {
            color: root.surfaceColor
            anchors.centerIn: parent
            width: root.sw * 0.76875   //492
            height: root.sh * 0.2833333 //136

            Column {
                id: confirmColumn
                anchors.fill: parent
                spacing: root.sh * 0.05 //24

                Text {
                    text: "RUN " + (itemsRoot.pendingScript ? itemsRoot.pendingScript.name : "") + "?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: confirmOverlay.choices
                        delegate: Item {
                            width: confirmColumn.width
                            height: root.sh * 0.0583333 //28

                            Rectangle {
                                anchors.fill: choiceText
                                color: root.accentColor
                                visible: index === confirmOverlay.choiceIndex
                            }

                            Text {
                                id: choiceText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: index === confirmOverlay.choiceIndex ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.capitalization: Font.AllUppercase
                                topPadding: root.sh * 0.0041667 //2
                                leftPadding: root.sw * 0.009375 //6
                                rightPadding: root.sw * 0.009375 //6
                                bottomPadding: root.sh * 0.00625 //3
                                font.pixelSize: root.sh * 0.05 //24
                            }
                        }
                    }
                }

                Text {
                    text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":RUN"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
