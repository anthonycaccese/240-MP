import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    property string moduleId: "com.240mp.dlna"

    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    property var navStack: []
    property var currentParams: ({})

    // Shared data
    property var servers: []
    property var currentServer: null

    // Browse position lives here rather than in BrowseView: opening DetailView
    // unloads BrowseView, so anything held there is lost on the way back.
    property var    folderStack: []
    property string currentContainerId: "0"
    property string currentFolder: ""
    property bool   categorySkipped: false
    property bool   pinResolved: false
    property var currentItems: []

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    function navigateBack() {
        if (navStack.length === 0) {
            moduleRoot.goBack()
            return
        }
        var prev = navStack.pop()
        if (!prev.source || prev.source.toString() === "") {
            moduleRoot.goBack()
            return
        }
        var restored = Object.assign({}, prev.params)
        restored.navListState = prev.listState || {}
        currentParams = restored
        internalLoader.setSource(prev.source, { "navParams": restored })
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        onLoaded: {
            console.log("DLNA: Loader loaded:", internalLoader.source)
            if (item) item.forceActiveFocus()
        }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) {
                console.log("DLNA: navigateTo", path)
                moduleRoot.navigateTo(path, params, listState)
            }
            function onGoBack() {
                console.log("DLNA: goBack signal received")
                moduleRoot.navigateBack()
            }
        }
    }

    Connections {
        target: dlnaBackend

        function onServerDiscovered(udn, name) {
            moduleRoot.servers = dlnaBackend.getDiscoveredServers()
        }

        function onItemsLoaded() {
            moduleRoot.currentItems = dlnaBackend.getCurrentItems()
        }

    }

    Component.onCompleted: {
        // A pinned main-menu row arrives as navParams (see get_menu_entries) and
        // goes straight to the folder it names. ServerSelectionView starts its
        // own discovery, so there is nothing to kick off here.
        if (navParams && navParams.serverUdn) {
            // A pin names its destination outright, so no category shortcut and
            // no trail above it.
            folderStack = []
            currentContainerId = navParams.objectId || "0"
            currentFolder = navParams.title || navParams.serverName || ""
            categorySkipped = true
            pinResolved = false
            navigateTo("BrowseView.qml", {
                pinned: true,
                serverUdn: navParams.serverUdn,
                serverName: navParams.serverName,
                objectId: navParams.objectId,
                title: navParams.title
            })
        } else {
            navigateTo("ServerSelectionView.qml", {})
        }
    }
}
