import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    readonly property var currentScript: scriptList.model[scriptList.currentIndex] || null

    focus: true

    // TODO(Phase 3): route to Console.qml / Takeover.qml (honouring meta.confirm).
    // Until then the list browses and shows each script's parsed sidecar settings,
    // which is what makes the sidecar format verifiable on its own.
    function runScript(entry) {
        if (!entry) return
        console.log("[Scripts] run not yet implemented: " + entry.basename
                    + " (mode=" + entry.mode + ")")
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

        Keys.onReturnPressed: itemsRoot.runScript(scriptList.model[scriptList.currentIndex])

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
