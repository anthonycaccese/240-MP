import QtQuick

FocusScope {
    id: moduleRoot

    // Exit signal — emitted to leave the module entirely
    signal goBack()

    property var navParams: ({})

    // The module's manifest id — the single place it appears in this module's QML.
    // Child views reference it via moduleRoot.moduleId.
    property string moduleId: "com.240mp.jellyfin"
    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    // Internal navigation state
    property var navStack: []
    property var currentParams: ({})

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    function replaceWith(viewPath, params) {
        var resolved = Qt.resolvedUrl(viewPath)
        currentParams = params || {}
        navStack = []
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    // Swap the current view in place without touching navStack — used when a
    // view (e.g. Boxset.qml with a single content type) hands off to another
    // without inserting itself into the back history. The existing stack entry
    // still points at whatever opened this view, so BACK skips the swapped-out
    // view entirely. (replaceWith, by contrast, wipes the whole stack.)
    function replaceCurrent(viewPath, params) {
        var resolved = Qt.resolvedUrl(viewPath)
        currentParams = params || {}
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    // Repoint the BACK target after autoplay advances in place. The top of the
    // stack is the detail view the player was launched from; swap its item so
    // exiting the player returns to the now-playing episode's detail screen.
    function updateBackItem(item) {
        if (navStack.length === 0) return
        var top = navStack[navStack.length - 1]
        top.params = Object.assign({}, top.params, { item: item })
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
            function onReplaceWith(path, params) { moduleRoot.replaceWith(path, params) }
            function onReplaceCurrent(path, params) { moduleRoot.replaceCurrent(path, params) }
            function onGoBack() { moduleRoot.navigateBack() }
            function onUpdateBackItem(item) { moduleRoot.updateBackItem(item) }
        }
    }

    // Handle auth state and logout from backend
    Connections {
        target: jellyfinBackend
        function onLogoutComplete() {
            moduleRoot.navStack = []
            moduleRoot.navigateTo("Auth.qml", {}, {})
        }
        function onAuthRevoked() {
            moduleRoot.navStack = []
            moduleRoot.navigateTo("Auth.qml", {}, {})
        }
        function onAuthStateChanged() {
            var state = jellyfinBackend.get_auth_state()
            if (state === "authed") {
                moduleRoot.navStack = []
                moduleRoot.replaceWith("Libraries.qml", {})
            } else {
                moduleRoot.navStack = []
                moduleRoot.replaceWith("Auth.qml", {})
            }
        }
    }

    Component.onCompleted: {
        // Show an initial view synchronously so the module always renders a
        // screen, even if the background token check stalls on an unreachable
        // server. check_auth() then validates the stored token; onAuthStateChanged
        // (and onAuthRevoked) re-route to Auth.qml if it was revoked.
        if (jellyfinBackend.get_auth_state() === "authed") {
            replaceWith("Libraries.qml", {})
            jellyfinBackend.check_auth()
        } else {
            replaceWith("Auth.qml", {})
        }
    }
}
