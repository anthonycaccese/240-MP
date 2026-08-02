import QtQuick

// The display host: owns the screen carousel, key handling and the status bar.
//
// Rotation lives here rather than in the module's nav stack because the screens
// are peers in a ring, not a hierarchy — you never "go back" from Almanac to
// Current Conditions, you just keep going round.
FocusScope {
    id: weatherRoot

    property var navParams: ({})
    signal replaceWith(string path, var params)
    signal goBack()

    // Every screen this module can show, in rotation order. `screens` below is
    // this list filtered by the Displays setting — the ring, the timer and the
    // key handling all work off the filtered list, so adding a screen is one
    // entry here plus one option in the backend's getDisplays().
    readonly property var allScreens: [
        { id: "current",  source: "screens/CurrentConditions.qml" },
        { id: "extended", source: "screens/ExtendedForecast.qml"  },
        { id: "others",   source: "screens/OtherLocations.qml"    },
        { id: "almanac",  source: "screens/Almanac.qml"           }
    ]
    property var screens: []
    property int  screenIndex: 0
    property bool paused: false
    property int  screenTimeMs: 10000

    property bool twelveHour: false

    focus: true

    Component.onCompleted: {
        var cfg = appCore.get_settings()
        var mod = (cfg.modules && cfg.modules[moduleRoot.moduleId]) || {}
        twelveHour = (mod.hours_format || "24-hour").indexOf("12") === 0
        // parseInt stops at the space, so "30 SEC" -> 30.
        screenTimeMs = (parseInt(mod.screen_time || "10") || 10) * 1000

        // Suppress the screen saver for as long as this view is up. The module
        // IS a screen saver — a rotating display you leave running — so letting
        // the saver paint over it would be actively wrong. The flag is
        // mpv-named because playback was its first user; it is the app's
        // general "something is already owning the screen" signal.
        idleTracker.mpvActive = true

        // Absent means enabled, matching MultiSelectSettings.qml, which defaults
        // an option to true when config has no entry for it.
        chosenDisplays = mod.displays || {}
        rebuildRing()

        weatherBackend.start()
        // No-op when the Music setting is off, so the view doesn't have to read
        // the setting a second time.
        weatherBackend.startMusic()
    }

    property var chosenDisplays: ({})

    // A screen is in the ring if it is enabled AND has something to show. Only
    // "others" can be empty — it depends on extra lines in weather_location.txt,
    // which are not known until the extras have resolved, so this reruns on
    // every data update rather than once at startup.
    function screenHasContent(id) {
        if (id === "others") return weatherBackend.hasOtherLocations
        return true
    }

    function rebuildRing() {
        var ring = []
        for (var i = 0; i < allScreens.length; i++) {
            var s = allScreens[i]
            var enabled = (chosenDisplays[s.id] === undefined) || chosenDisplays[s.id]
            if (enabled && screenHasContent(s.id)) ring.push(s.source)
        }
        // Never leave an empty ring — a module that shows nothing is worse than
        // one that ignores a setting.
        screens = ring.length > 0 ? ring : [ allScreens[0].source ]
        if (screenIndex >= screens.length) screenIndex = 0
    }

    Component.onDestruction: {
        if (idleTracker) {
            idleTracker.mpvActive = false
            idleTracker.resetActivity()
        }
    }

    function advance(step) {
        var n = weatherRoot.screens.length
        screenIndex = (screenIndex + step + n) % n
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                || event.key === Qt.Key_Back) {
            weatherRoot.goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            // Stepping manually pauses the loop, matching the original's
            // behaviour: you stepped somewhere because you wanted to look at it.
            weatherRoot.paused = true
            advance(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            weatherRoot.paused = true
            advance(1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            weatherRoot.paused = !weatherRoot.paused
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            // Independent of weatherRoot.paused: Enter pauses the carousel, and
            // music should keep playing under a screen you're lingering on.
            weatherBackend.toggleMusic()
            event.accepted = true
        }
    }

    Timer {
        interval: weatherRoot.screenTimeMs
        running: !weatherRoot.paused && weatherRoot.screens.length > 1
                 && weatherBackend.hasData
        repeat: true
        onTriggered: weatherRoot.advance(1)
    }

    Connections {
        target: weatherBackend
        function onLocationError(reason) {
            // replaceWith, not navigateTo: pushing this view onto the back stack
            // would make BACK from the setup screen return here and immediately
            // retry the same failing lookup, trapping the user in a loop.
            //
            // Music stops here too: playing it over "your location file is
            // missing" would be absurd.
            weatherBackend.stopMusic()
            weatherRoot.replaceWith("Setup.qml", { reason: reason })
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
    }

    Text {
        anchors.centerIn: parent
        visible: !weatherBackend.hasData
        text: "LOADING..."
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.05 //24
    }

    // Screen area, above the status bar.
    Loader {
        id: screenLoader
        visible: weatherBackend.hasData
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: statusBar.top
        anchors.leftMargin: root.sw * 0.125 //80
        anchors.rightMargin:  root.sw * 0.125 //80
        anchors.topMargin: root.sh * 0.125 //60
        anchors.bottomMargin: root.sh * 0.02

        source: weatherRoot.screens.length > 0
                ? weatherRoot.screens[weatherRoot.screenIndex] : ""
        onLoaded: syncScreen()

        // Screens declare only the properties they use, so assign defensively
        // rather than assuming a shape.
        function syncScreen() {
            if (!item) return
            if (item.hasOwnProperty("wx"))           item.wx = weatherBackend.current
            if (item.hasOwnProperty("locationName")) item.locationName = weatherBackend.locationName
            if (item.hasOwnProperty("days"))         item.days = weatherBackend.forecast
            if (item.hasOwnProperty("almanac"))      item.almanac = weatherBackend.almanac
            if (item.hasOwnProperty("rows"))         item.rows = weatherBackend.otherLocations
            if (item.hasOwnProperty("tempUnit"))     item.tempUnit = weatherBackend.tempUnitLabel
        }
    }

    Connections {
        target: weatherBackend
        function onDataChanged() {
            weatherRoot.rebuildRing()
            screenLoader.syncScreen()
        }
    }

    StatusBar {
        id: statusBar
        visible: weatherBackend.hasData
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sw * 0.125 //80
        anchors.rightMargin:  root.sw * 0.125 //80
        anchors.bottomMargin: root.sh * 0.1041667 //50

        utcOffsetSeconds: weatherBackend.utcOffsetSeconds
        twelveHour: weatherRoot.twelveHour
        lines: {
            var c = weatherBackend.current
            if (!weatherBackend.hasData) return []
            return [
                weatherBackend.locationName,
                "CURRENTLY: " + c.condition + " " + c.temperature,
                "HUMIDITY: " + c.humidity,
                "DEWPOINT: " + c.dewPoint,
                "PRESSURE: " + c.pressure,
                "WIND: " + c.wind,
                "VISIBILITY: " + c.visibility
            ]
        }
    }
}
