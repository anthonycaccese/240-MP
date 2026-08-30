import QtQuick

FocusScope {
    id: wxRoot

    property var navParams: ({})

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

        var next = dialRows[(at + direction + dialRows.length) % dialRows.length]
        if (!next || next.number === myNumber()) return

        if (next.special === "guide") {
            navigateTo("Guide.qml", {}, { fromWeather: true })
        } else if (next.special === "weather") {
            return
        } else {
            navigateTo("Player.qml", {
                channelNumber: next.number,
                channelName:   next.name
            }, { fromWeather: true })
        }
    }

    Component.onCompleted: buildDial()

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_ChannelUp || event.key === Qt.Key_PageUp) {
            step(1); event.accepted = true
        } else if (event.key === Qt.Key_ChannelDown || event.key === Qt.Key_PageDown) {
            step(-1); event.accepted = true
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                   || event.key === Qt.Key_Back) {
            exitModule()
            event.accepted = true
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
            function onGoBack() { wxRoot.exitModule() }
        }
    }
}
