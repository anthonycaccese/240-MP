import QtQuick

// Module router. Opens on the channel guide; picking a channel hands off to
// Session.qml, which owns the long-lived mpv session.
FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    property string moduleId: "com.240mp.tv_mode"
    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    property var navStack: []
    property var currentParams: ({})

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
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) { moduleRoot.navigateTo(path, params, listState) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    Component.onCompleted: {
        // No channel-list screen: a TV does not ask which channel you want, it
        // comes on already showing something. Entry lands on a random channel
        // with the guide overlay up, so the lineup is still right there.
        var n = tvModeBackend ? tvModeBackend.channelCount() : 0
        var start = n > 0 ? Math.floor(Math.random() * n) : 0
        navigateTo("Session.qml", { startIndex: start, openGuide: true })
    }
}
