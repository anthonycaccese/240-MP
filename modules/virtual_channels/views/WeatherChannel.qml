import QtQuick

FocusScope {
    id: wxRoot

    property var navParams: ({})

    property string moduleId: navParams.moduleId || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()
    signal exitModule()

    focus: true

    property var dialRows: []

    function buildDial() {
        dialRows = virtualChannelsBackend.list_channels()
    }

    function myNumber() { return virtualChannelsBackend.weather_channel_number() }

    function step(direction) {
        if (dialRows.length === 0) buildDial()
        if (dialRows.length === 0) return

        var at = -1
        for (var i = 0; i < dialRows.length; i++)
            if (dialRows[i].number === myNumber()) { at = i; break }
        if (at < 0) at = 0

        goTo(dialRows[(at + direction + dialRows.length) % dialRows.length])
    }

    // Digits typed on the remote; a number that is a channel leaves for it.
    // Enter and Back belong to the forecast underneath, so here the number
    // goes by itself once the digits have settled.
    ChannelEntry {
        id: channelEntry
        dial: wxRoot.dialRows.map(function(r) { return r.number })
        onChosen: function(number) {
            for (var i = 0; i < dialRows.length; i++)
                if (dialRows[i].number === number) { goTo(dialRows[i]); return }
        }
    }

    function goTo(next) {
        if (!next || next.number === myNumber()) return

        if (next.special === "guide") {
            navigateTo("Guide.qml", {}, { fromWeather: true })
        } else if (next.special === "weather") {
            return
        } else {
            navigateTo("Player.qml", {
                moduleId:      wxRoot.moduleId,
                channelNumber: next.number,
                channelName:   next.name
            }, { fromWeather: true })
        }
    }

    Component.onCompleted: { buildDial(); loadVolume() }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_ChannelUp || event.key === Qt.Key_PageUp) {
            step(1); event.accepted = true
        } else if (event.key === Qt.Key_ChannelDown || event.key === Qt.Key_PageDown) {
            step(-1); event.accepted = true
        } else if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            if (dialRows.length === 0) buildDial()
            channelEntry.press(event.key, event.isAutoRepeat)
            event.accepted = true
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                   || event.key === Qt.Key_Back) {
            exitModule()
            event.accepted = true
        }
    }

    // The weather channel's sound is the weather module's music, and mpv is not
    // running at all here, so the volume keys are this screen's to answer.
    property int masterVolume: 100

    function loadVolume() {
        var v = appCore.get_setting(moduleId, "volume")
        if (v !== undefined && v !== null && String(v) !== "")
            masterVolume = Math.max(0, Math.min(100, parseInt(v)))
        volumeOsd.level = masterVolume
        applyVolume()
    }

    function applyVolume() {
        if (typeof weatherBackend === "undefined" || !weatherBackend) return
        weatherBackend.set_music_volume(volumeOsd.muted ? 0 : masterVolume)
    }

    VolumeOsd {
        id: volumeOsd
        moduleId: wxRoot.moduleId
        z: 100
        onAdjusted: {
            wxRoot.masterVolume = level
            wxRoot.applyVolume()
        }
    }

    Loader {
        id: wxLoader
        anchors.fill: parent
        focus: true
        source: Qt.resolvedUrl("../../weather/views/Root.qml")
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: wxLoader.item
            ignoreUnknownSignals: true
            // Back while a number is being typed takes the number back, not the viewer out.
            function onGoBack() {
                if (channelEntry.digits !== "") channelEntry.clear()
                else wxRoot.exitModule()
            }
        }
    }
}
