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

    // Screens in rotation order. Grows as the other three are built; the ring
    // and the key handling do not change when it does.
    readonly property var screens: [ "screens/CurrentConditions.qml" ]
    property int  screenIndex: 0
    property bool paused: false
    property int  screenTimeMs: 10000

    property bool twelveHour: false

    focus: true

    Component.onCompleted: {
        var cfg = appCore.get_settings()
        var mod = (cfg.modules && cfg.modules[moduleRoot.moduleId]) || {}
        twelveHour = (mod.hours_format || "24-hour").indexOf("12") === 0
        weatherBackend.start()
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
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.06
    }

    // Screen area, above the status bar.
    Loader {
        id: screenLoader
        visible: weatherBackend.hasData
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: statusBar.top
        anchors.leftMargin:   root.sw * 0.055
        anchors.rightMargin:  root.sw * 0.055
        anchors.topMargin:    root.sh * 0.075
        anchors.bottomMargin: root.sh * 0.02

        source: weatherRoot.screens[weatherRoot.screenIndex]
        onLoaded: syncScreen()

        function syncScreen() {
            if (!item) return
            item.wx = weatherBackend.current
            item.locationName = weatherBackend.locationName
        }
    }

    Connections {
        target: weatherBackend
        function onDataChanged() { screenLoader.syncScreen() }
    }

    StatusBar {
        id: statusBar
        visible: weatherBackend.hasData
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin:   root.sw * 0.055
        anchors.rightMargin:  root.sw * 0.055
        anchors.bottomMargin: root.sh * 0.055

        utcOffsetSeconds: weatherBackend.utcOffsetSeconds
        twelveHour: weatherRoot.twelveHour
        lines: {
            var c = weatherBackend.current
            if (!weatherBackend.hasData) return []
            return [
                "CONDITIONS AT " + weatherBackend.locationName,
                "HUMIDITY: " + c.humidity + "  DEWPOINT: " + c.dewPoint,
                "VISIB: " + c.visibility
            ]
        }
    }
}
