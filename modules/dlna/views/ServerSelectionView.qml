import QtQuick
import Components

FocusScope {
    id: view

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

    // Discovery state, surfaced through the AppBar subtitle so the list can sit
    // at the same fixed offset the other modules use.
    property string status: "Discovering servers..."
    property bool scanning: true

    // Header
    AppBar {
        id: header
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: view.status
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    // Empty state
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333
        visible: serverList.count === 0 && !view.scanning

        Text {
            text: "No servers found"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.05
        }
        Text {
            text: "Check your network connection"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333
        }
    }

    // Server list
    ListView {
        id: serverList
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        keyNavigationEnabled: true
        clip: true
        focus: true
        spacing: root.sh * 0.0166667

        model: moduleRoot.servers

        delegate: Rectangle {
            width: serverList.width
            height: root.sh * 0.0833333
            color: ListView.isCurrentItem ? root.accentColor : "transparent"
            radius: root.sh * 0.0083333

            Text {
                text: view.pinMark(modelData.udn) + (modelData.name || "DLNA Server")
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0416667
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0292969
            }

            Text {
                // Address only. The manufacturer field is vendor boilerplate —
                // MiniDLNA reports its author's name there, for instance.
                text: modelData.address || ""
                // Grey is unreadable against the highlight, so the selected row
                // matches the server name.
                color: parent.ListView.isCurrentItem ? root.primaryColor : root.secondaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.0292969
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    serverList.currentIndex = index
                    view.openServer(modelData)
                }
            }
        }

        // Controller/keyboard activation belongs on the ListView — delegates
        // never hold key focus.
        Keys.onReturnPressed: view.openServer(moduleRoot.servers[serverList.currentIndex])

        Keys.onPressed: function(event) {
            // Right, not Space: matches the scripts and YouTube modules.
            if (event.key === Qt.Key_Right) {
                var server = moduleRoot.servers[serverList.currentIndex]
                // "0" is the ContentDirectory root, so a server pin opens the
                // same place selecting the server would.
                if (server)
                    dlnaBackend.togglePin(server.udn, server.name, "0", server.name)
                event.accepted = true
            }
        }
    }

    // Bumped on pinsChanged so the "* " markers re-evaluate.
    property int pinRev: 0

    function pinMark(udn) {
        if (pinRev < 0)
            return ""
        return dlnaBackend.isPinned(udn, "0") ? "* " : ""
    }

    // BrowseView issues the browse itself once it loads, so this only records
    // the selection and navigates.
    function openServer(server) {
        if (!server)
            return
        dlnaBackend.selectServer(server.udn)
        moduleRoot.currentServer = server

        // Fresh descent through this server.
        moduleRoot.folderStack = []
        moduleRoot.currentContainerId = "0"
        moduleRoot.currentFolder = server.name
        moduleRoot.categorySkipped = false
        moduleRoot.pinResolved = false
        view.navigateTo("BrowseView.qml", { server: server }, { currentIndex: serverList.currentIndex })
    }

    Connections {
        target: dlnaBackend

        function onDiscoveryStarted() {
            view.scanning = true
            view.status = "Discovering servers..."
        }

        function onDiscoveryFinished() {
            refreshServers()
            view.scanning = false
            view.status = moduleRoot.servers.length > 0
                ? `${moduleRoot.servers.length} server(s)`
                : "No servers found"
        }

        function onServerDiscovered(udn, name) {
            refreshServers()
        }

        function onPinsChanged() {
            view.pinRev++
        }

        function onConnectionError(error) {
            view.scanning = false
            view.status = error
        }
    }

    // Footer
    Text {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + root.hints.browse + ":PIN " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }

    // Always pull the full list rather than accumulating from the signal:
    // a re-scan suppresses serverDiscovered for servers already seen, so a
    // signal-only list comes back empty when this view is re-entered.
    function refreshServers() {
        moduleRoot.servers = dlnaBackend.getDiscoveredServers()
    }

    Component.onCompleted: {
        refreshServers()
        if (navListState.currentIndex !== undefined && moduleRoot.servers.length > 0)
            serverList.currentIndex = Math.min(navListState.currentIndex,
                                               moduleRoot.servers.length - 1)
        if (moduleRoot.servers.length > 0)
            view.status = `${moduleRoot.servers.length} server(s)`
        dlnaBackend.startDiscovery()
        serverList.forceActiveFocus()
    }
}
