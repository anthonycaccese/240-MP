import QtQuick
import Components

// Channel groups (categories) from the M3U's group-title tags, plus an
// "ALL CHANNELS" shelf (prepended by the backend) and a "CHANGE PLAYLIST" entry.
FocusScope {
    id: groupsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var groups: []
    property bool loading: true
    property string errorMsg: ""

    // Sentinel row appended after the real groups.
    readonly property var changeEntry: ({ title: "CHANGE PLAYLIST", key: "__change__" })

    property var rows: groups.concat([changeEntry])

    Connections {
        target: iptvBackend

        function onGroupsLoaded(items) {
            groupsRoot.groups = items
            groupsRoot.loading = false
            var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
            groupList.currentIndex = Math.min(restore, groupsRoot.rows.length - 1)
            groupList.positionViewAtIndex(groupList.currentIndex, ListView.Contain)
        }

        function onLoadingChanged(isLoading) {
            if (isLoading) groupsRoot.loading = true
        }

        function onPlaylistError(msg) {
            groupsRoot.loading = false
            groupsRoot.errorMsg = msg
        }
    }

    Component.onCompleted: iptvBackend.load_groups(false)

    focus: true

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Channels"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Text {
        visible: loading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }

    // Error is non-blocking: the list still shows the "CHANGE PLAYLIST" row so a
    // bad URL can be corrected without leaving the module.
    Text {
        visible: !loading && errorMsg !== ""
        text: errorMsg
        color: root.accentColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        anchors.bottom: groupsFooter.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: root.sh * 0.0333333
        width: root.sw * 0.6
        wrapMode: Text.WordWrap
        font.pixelSize: root.sh * 0.0333333
    }

    ListView {
        id: groupList
        model: groupsRoot.rows
        visible: !loading
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            groupList.positionViewAtIndex(groupList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            groupList.positionViewAtIndex(groupList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            var g = groupsRoot.rows[currentIndex]
            if (!g) return
            if (g.key === "__change__") {
                groupsRoot.navigateTo("Setup.qml", {}, { currentIndex: groupList.currentIndex })
                return
            }
            groupsRoot.navigateTo("Channels.qml", {
                group: g.key,
                title: g.title
            }, { currentIndex: groupList.currentIndex })
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                groupsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: groupList.width
            height: root.sh * 0.0583333

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, groupList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: groupList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: (modelData.title || "") +
                          (modelData.count !== undefined ? "  (" + modelData.count + ")" : "")
                    color: groupList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    rightPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.05
                }

                SequentialAnimation {
                    running: (groupList.currentIndex === index) &&
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

    Text {
        id: groupsFooter
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
