import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: root
    flags: Qt.FramelessWindowHint | Qt.Window
    x:      Qt.platform.os === "osx" ? macScreenX      : Screen.virtualX
    y:      Qt.platform.os === "osx" ? macScreenY      : Screen.virtualY
    width:  Qt.platform.os === "osx" ? macScreenWidth  : Screen.width
    height: Qt.platform.os === "osx" ? macScreenHeight : Screen.height
    visible: true
    color: root.surfaceColor

    // --- Color Schemes ---
    readonly property var themes: ({
        "Video 1": {
            "primary": "#FFFFFF",
            "secondary": "#C2BFE4",
            "tertiary": "#8480C9",
            "surface": "#0A0094",
            "accent": "#AECFFF"
        },
        "Late Night": {
            "primary": "#FFFFFF",
            "secondary": "#A1A1A1",
            "tertiary": "#444444",
            "surface": "#000000",
            "accent": "#FFD900"
        },
        "Synthwave": {
            "primary": "#FFFFFF",
            "secondary": "#D48BFF",
            "tertiary": "#7836B5",
            "surface": "#12012B",
            "accent": "#00E5FF"
        },
        "Terminal": {
            "primary": "#4AF626",
            "secondary": "#32A81B",
            "tertiary": "#1A590E",
            "surface": "#000000",
            "accent": "#4AF626"
        },
        "T-120": {
            "primary": "#000000",
            "secondary": "#818181",
            "tertiary": "#df9c27",
            "surface": "#FAF5E8",
            "accent": "#EE442F"
        },
        "Amber": {
            "primary": "#FFB000",
            "secondary": "#B37B00",
            "tertiary": "#B37B00",
            "surface": "#000000",
            "accent": "#FFEE11"
        },
        "Kinescope": {
            "primary": "#FFFFFF",
            "secondary": "#9E9E9E",
            "tertiary": "#424242",
            "surface": "#121212",
            "accent": "#FFFFFF"
        },
        "SMPTE ECR 1-1978": {  // 75% max 0xFF == 0xBF, 40% max 0xFF == 0x66, 7.5% max 0xFF == 0x13; 75/7.5 targets per https://en.wikipedia.org/wiki/SMPTE_color_bars#Analog_NTSC - mixed with 40% in "off channels" to both wash out and improve contrast
            "primary": "#BFBFBF",
            "secondary": "#66BF66",
            "tertiary": "#6666BF",
            "surface": "#131313",
            "accent": "#BF6666"
        }
    })
    property var allThemes: themes  // may gain a "Custom" entry on startup
    property string currentTheme: "Video 1"
    property string primaryColor:   (allThemes[currentTheme] || allThemes["Video 1"]).primary
    property string secondaryColor: (allThemes[currentTheme] || allThemes["Video 1"]).secondary
    property string tertiaryColor:  (allThemes[currentTheme] || allThemes["Video 1"]).tertiary
    property string surfaceColor:   (allThemes[currentTheme] || allThemes["Video 1"]).surface
    property string accentColor:    (allThemes[currentTheme] || allThemes["Video 1"]).accent

    readonly property real sw: width
    readonly property real sh: height

    Connections {
        target: appCore
        function onAppSettingChanged(key, value) {
            if (key === "color_scheme") {
                root.currentTheme = value
            } else if (key === "screensaver_timeout") {
                var sec = parseInt(value)
                if (sec > 0) {
                    idleTracker.threshold = sec
                    idleTracker.enabled = true
                } else {  // "OFF"
                    idleTracker.enabled = false
                    if (screenSaverActive) screenSaverActive = false
                }
            }
        }
    }

    Component.onCompleted: {
        var cfg = appCore.get_settings()

        var cThemes = appCore.getCustomColorSchemes()
        if (Object.keys(cThemes).length > 0) {
            var t = Object.assign({}, themes, root.allThemes)
            for (var cTheme in cThemes) {
                if (Object.keys(cThemes[cTheme]).length === 5) {
                    t[cTheme] = cThemes[cTheme]
                }
            }
            root.allThemes = t
        }

        var custom = appCore.getCustomColorScheme()
        if (Object.keys(custom).length === 5) {
            var t = Object.assign({}, themes, root.allThemes)
            t["Custom"] = custom
            root.allThemes = t
        }

        var savedTheme = (cfg.app && cfg.app.color_scheme) || "Video 1"
        if (savedTheme === "Custom" && !root.allThemes["Custom"]) {
            appCore.save_setting("", "color_scheme", "Video 1")
            savedTheme = "Video 1"
        }
        root.currentTheme = savedTheme

        // Screensaver: the tracker starts disabled; this is the single place the
        // saved setting is applied (live changes land in onAppSettingChanged above,
        // mirroring color_scheme). parseInt("OFF") is NaN, so OFF stays disabled.
        var ssSec = parseInt(cfg.app && cfg.app.screensaver_timeout)
        if (ssSec > 0) {
            idleTracker.threshold = ssSec
            idleTracker.enabled = true
        }

        // Break declarative bindings on macOS so the C++ NSWindow override
        // in forceWindowFullScreen() isn't immediately re-fought by QML.
        if (Qt.platform.os === "osx") {
            root.x = macScreenX
            root.y = macScreenY
            root.width = macScreenWidth
            root.height = macScreenHeight
        }
    }
    
    FontLoader {
        id: font; source: "assets/fonts/VCR_OSD_MONO_1.001.ttf"
    }
    property string globalFont: font.name;

    // --- INPUT / APP INFO MIRRORS ---
    // Views must bind these via `root.*`, never the appCore/inputManager
    // context properties directly: when the module Loader swaps views, the
    // dying view's context properties resolve to null and any binding on them
    // throws a TypeError during teardown. id-resolved `root.*` stays valid
    // (root lives as long as the app), so these mirrors are teardown-safe.
    // The null guards absorb the same nulling here at app shutdown, when the
    // engine invalidates the root context itself.
    readonly property var hints: inputManager ? inputManager.hints : ({})
    readonly property string appVersion: appCore ? appCore.appVersion : ""

    // --- SCREEN SAVER STATE ---
    property bool screenSaverActive: false

    // --- APP-LEVEL NAV STACK ---
    property var appNavStack: []
    property var appCurrentParams: ({})
    property bool _startupNavigated: false

    // --- MPV PLAYBACK TRACKING ---
    // Block the screen saver while mpv is playing so it never flashes during or
    // immediately after playback. The core guard is in IdleTracker (mpvActive
    // property), which also resets the idle timer on transitions.
    Connections {
        target: mpvController
        function onPositionChanged(ms) {
            if (ms > 0 && !idleTracker.mpvActive) {
                idleTracker.mpvActive = true
                idleTracker.resetActivity()
            }
        }
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            idleTracker.mpvActive = false
            idleTracker.resetActivity()
        }
    }

    // --- MODULE LOADER ---
    Loader {
        id: moduleLoader;
        anchors.fill: parent;
        focus: true;
        source: "views/ModuleList.qml";

        Keys.onPressed: (event) => {
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Q) {
                Qt.quit()
            }
        }

        onLoaded: {
            item.forceActiveFocus()
            if (!root._startupNavigated) {
                root._startupNavigated = true
                var entryPoint = appCore.startupModuleEntryPoint()
                if (entryPoint) {
                    root.appNavStack.push({
                        source: moduleLoader.source,
                        params: root.appCurrentParams,
                        listState: {}
                    })
                    moduleLoader.setSource(entryPoint, { "navParams": {} })
                }
            }
        }

        Connections {
            target: moduleLoader.item
            ignoreUnknownSignals: true

            function onNavigateTo(path, params, listState) {
                root.appNavStack.push({ source: moduleLoader.source, params: root.appCurrentParams, listState: listState || {} })
                root.appCurrentParams = params || {}
                moduleLoader.setSource(path, { "navParams": params || {} })
            }

            function onGoBack() {
                if (root.appNavStack.length === 0) return
                var prev = root.appNavStack.pop()
                root.appCurrentParams = prev.params
                moduleLoader.setSource(prev.source, { "navParams": prev.params, "navListState": prev.listState || {} })
            }

        }
    }

    // --- SCREEN SAVER (Idle Tracker integration) ---
    Connections {
        target: idleTracker
        function onActiveChanged() {
            // Only show on active → true; never hide here — the overlay's
            // key handler owns dismissal, preventing the C++ event filter's
            // synchronous reset from stealing the key from QML.
            if (idleTracker.active && idleTracker.enabled) {
                if (!screenSaverActive) {
                    var usableW = screenSaverOverlay.width - bounceLogo.width
                    var usableH = screenSaverOverlay.height - bounceLogo.height
                    bounceLogo.x = Math.random() * (usableW > 0 ? usableW : 1)
                    bounceLogo.y = Math.random() * (usableH > 0 ? usableH : 1)
                    bounceLogo.vx = (Math.random() > 0.5 ? 1 : -1) * (1 + Math.random() * 1.5)
                    bounceLogo.vy = (Math.random() > 0.5 ? 1 : -1) * (1 + Math.random() * 1.5)
                    screenSaverActive = true
                    screenSaverOverlay.forceActiveFocus()
                }
            }
        }
    }

    Item {
        id: screenSaverOverlay
        anchors.fill: parent
        visible: screenSaverActive
        z: 9999
        focus: visible

        // Solid black background — no transparency so it serves as a true
        // CRT burn-in prevention black frame between the logo bounces.
        Rectangle {
            anchors.fill: parent
            color: "#000000"
        }

        // Bouncing logo — classic DVD player screen saver
        Image {
            id: bounceLogo
            source: "assets/images/logo.svg"
            sourceSize.width: root.sw * 0.05
            sourceSize.height: root.sw * 0.05
            fillMode: Image.PreserveAspectFit
            antialiasing: true

            property real vx: 0
            property real vy: 0

            // Physics tick at ~60 fps while the overlay is visible
            Timer {
                interval: 16
                repeat: true
                running: screenSaverActive
                onTriggered: {
                    bounceLogo.x += bounceLogo.vx
                    bounceLogo.y += bounceLogo.vy

                    if (bounceLogo.x + bounceLogo.width > screenSaverOverlay.width) {
                        bounceLogo.x = screenSaverOverlay.width - bounceLogo.width
                        bounceLogo.vx = -Math.abs(bounceLogo.vx)
                    } else if (bounceLogo.x < 0) {
                        bounceLogo.x = 0
                        bounceLogo.vx = Math.abs(bounceLogo.vx)
                    }

                    if (bounceLogo.y + bounceLogo.height > screenSaverOverlay.height) {
                        bounceLogo.y = screenSaverOverlay.height - bounceLogo.height
                        bounceLogo.vy = -Math.abs(bounceLogo.vy)
                    } else if (bounceLogo.y < 0) {
                        bounceLogo.y = 0
                        bounceLogo.vy = Math.abs(bounceLogo.vy)
                    }
                }
            }
        }

        // Capture any keypress to dismiss — consumes the event so the
        // underlying view never sees it, preventing accidental navigation.
        // Ctrl+Q still quits (moduleLoader's handler is a sibling, so it
        // can't see keys focused here — handle the chord directly).
        Keys.onPressed: (event) => {
            event.accepted = true
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Q) {
                Qt.quit()
                return
            }
            screenSaverActive = false
            moduleLoader.forceActiveFocus()
        }
    }
}
