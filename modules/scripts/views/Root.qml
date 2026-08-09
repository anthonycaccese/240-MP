import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    // The module's manifest id — the single place it appears in this module's QML.
    // Child views reference it via moduleRoot.moduleId.
    property string moduleId: "com.240mp.scripts"
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
        // Entered from a main-menu favorite (AppCore passed the sidecar's basename
        // through as navParams.script): go straight to the runner, skipping the
        // list entirely.
        //
        // No auto-relaunch guard is needed. The runner's exit calls navigateBack(),
        // whose stack holds only the empty initial source, so it falls through to
        // goBack() and the user lands on the main menu — never back on a view that
        // would start the script again.
        if (navParams.script) {
            if (scriptsBackend.confirmFor(navParams.script)) {
                // A favorite must not bypass a gate the sidecar asked for. The
                // prompt already lives in Items.qml, so hand the script to it
                // rather than growing a second copy of that overlay.
                navigateTo("Items.qml", { confirmScript: navParams.script })
            } else {
                var mode = scriptsBackend.modeFor(navParams.script)
                // Translate to the runner views' own param names — they expect
                // "basename", and passing navParams straight through would hand them
                // an empty one.
                navigateTo(mode === "takeover" ? "Takeover.qml" : "Console.qml",
                           { basename: navParams.script, name: navParams.name })
            }
        } else if (navParams.fromAppStartup && startupScript !== "" && startupScript !== "None") {
            startupTimer.start()
        } else {
            navigateTo("Items.qml", navParams)
        }
    }

    // Auto-run on startup, when 240-MP booted straight into this module.
    readonly property string startupScript:
        appCore ? (appCore.get_setting(moduleId, "startup_script") || "") : ""

    // 750 ms rather than Qt.callLater. On a headless Pi a takeover hand-off has to
    // save Qt's CRTC state, and at first load Qt may not have completed a frame —
    // capturing then can yield an invalid state, which means the display could not
    // be restored afterwards. DisplayHandoff refuses the hand-off in that case
    // (falling back to console mode), but it is better not to provoke it.
    Timer {
        id: startupTimer
        interval: 750
        repeat: false
        onTriggered: {
            var mode = scriptsBackend.modeFor(moduleRoot.startupScript)
            if (mode === "") {
                console.log("[Scripts] startup_script no longer exists: "
                            + moduleRoot.startupScript)
                navigateTo("Items.qml", moduleRoot.navParams)
                return
            }
            // Deliberately bypasses "confirm": nobody is there to answer a prompt
            // on a machine that just booted into this script on purpose.
            navigateTo(mode === "takeover" ? "Takeover.qml" : "Console.qml",
                       { basename: moduleRoot.startupScript,
                         name: scriptsBackend.nameFor(moduleRoot.startupScript) })
        }
    }
}
