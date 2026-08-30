import QtQuick
import QtMultimedia

Item {
    id: preview

    readonly property bool active: player.source != ""
    property bool ready: false

    signal finished()

    property int volume: 100

    readonly property bool canPlayAudio: devices.audioOutputs.length > 0
    MediaDevices { id: devices }

    function show(url, positionMs) {
        if (url === undefined || url === null || String(url) === "") { clear(); return }
        if (String(player.source) === String(url)) {
            if (Math.abs(player.position - positionMs) > 4000) player.position = positionMs
            return
        }
        ready = false
        player.stop()
        player.source = url
        pendingPosition = positionMs
        player.play()
    }

    function clear() {
        ready = false
        pendingPosition = -1
        player.stop()
        player.source = ""
    }

    property real pendingPosition: -1

    MediaPlayer {
        id: player
        videoOutput: output
        audioOutput: AudioOutput {
            volume: Math.max(0, Math.min(1, preview.volume / 100))
        }

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia) {
                if (preview.pendingPosition >= 0) {
                    player.position = preview.pendingPosition
                    preview.pendingPosition = -1
                }
                preview.ready = true
            }
            if (mediaStatus === MediaPlayer.EndOfMedia) preview.finished()
        }

        onErrorOccurred: function(err, str) {
            console.log("[guide] preview could not play:", str)
            preview.clear()
        }
    }

    VideoOutput {
        id: output
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
        visible: preview.ready
    }

    Component.onDestruction: clear()
}
