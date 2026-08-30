import QtQuick

// Analogue snow, for the wait between one channel and the next.
//
// Four tiles rather than one, swapped and shifted every frame: a single tile
// held still reads as a texture, and the shift is what makes it read as noise.
// Tiles are drawn at their own size with smoothing off, so nothing is scaled
// and the grain stays as fine as it was authored.
Item {
    id: snowField

    property bool running: true

    readonly property int tileSize: 160
    readonly property int tileCount: 4
    property int tileIndex: 0
    property int shiftX: 0
    property int shiftY: 0

    Image {
        x: -snowField.shiftX
        y: -snowField.shiftY
        // Oversized by one tile so a shift of up to a whole tile still covers
        // the corner it is moving away from.
        width: snowField.width + snowField.tileSize
        height: snowField.height + snowField.tileSize
        fillMode: Image.Tile
        smooth: false
        source: "../assets/images/snow-" + (snowField.tileIndex + 1) + ".png"
    }

    Timer {
        interval: 50
        repeat: true
        running: snowField.running && snowField.visible
        onTriggered: {
            snowField.tileIndex = (snowField.tileIndex + 1) % snowField.tileCount
            snowField.shiftX = Math.floor(Math.random() * snowField.tileSize)
            snowField.shiftY = Math.floor(Math.random() * snowField.tileSize)
        }
    }
}
