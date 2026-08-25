import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()
    // Routes out of this module entirely, to another module's entry point — the
    // app shell handles this one (Main.qml), not the internal Loader below.
    signal navigateTo(string path, var params, var listState)

    property var navParams: ({})

    property string moduleId: "com.240mp.airplay"
    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    property var navStack: []
    property var currentParams: ({})

    function navigateToView(viewPath, params, fromState) {
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
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) { moduleRoot.navigateToView(path, params, listState) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    // AirPlay is a passive receiver, not something you browse — there's exactly
    // one view, so there's nothing to push onto navStack beyond it.
    Component.onCompleted: navigateToView("NowPlaying.qml", {})
}
