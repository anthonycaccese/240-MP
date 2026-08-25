import QtQuick
import QtQuick.Effects
import Components

// Single live view — the phone initiates, so there's nothing to browse into,
// same shape as nfc_reader/views/Items.qml (a passive status display) rather
// than the browse-then-play flow the media modules use.
FocusScope {
    id: nowPlaying

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    readonly property bool connected: airplayBackend.isConnected
    readonly property bool hasError: airplayBackend.receiverError.length > 0

    // No free-text setting type exists in the manifest schema (see
    // AirPlayBackend::deviceNameFilePath()'s comment), so "Album Art" and
    // "Allow Screen Saver" are read here directly rather than through any
    // generic settings-binding machinery; both re-evaluate on every
    // moduleSettingChanged rather than once, so toggling either while this
    // screen is open takes effect immediately.
    property bool albumArtEnabled: true
    function refreshAlbumArtSetting() {
        var v = appCore.get_setting(moduleRoot.moduleId, "album_art")
        albumArtEnabled = (v === undefined || v === null || v === "") ? true : (v === true || v === "ON")
    }

    // Default OFF: AirPlay blocks the screen saver like every other
    // screen-owning module (video playback, Weather's rotation) unless the
    // user explicitly opts in to letting it fire — audio doesn't need the
    // display kept awake, but the *default* preserves today's behavior
    // rather than surprising anyone already using this module.
    property bool allowScreenSaver: false
    function refreshAllowScreenSaverSetting() {
        var v = appCore.get_setting(moduleRoot.moduleId, "allow_screen_saver")
        allowScreenSaver = (v === true || v === "ON")
        idleTracker.mpvActive = !allowScreenSaver
    }

    Connections {
        target: appCore
        function onModuleSettingChanged(mid, key, value) {
            if (mid !== moduleRoot.moduleId) return
            if (key === "album_art") nowPlaying.refreshAlbumArtSetting()
            else if (key === "allow_screen_saver") nowPlaying.refreshAllowScreenSaverSetting()
        }
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
    }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    // --- Idle state: waiting for a phone/Mac to connect --------------------------------
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.025
        visible: !nowPlaying.connected && !nowPlaying.hasError

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: airplayBackend.deviceName
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0875
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Waiting for AirPlay connection…"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0333333
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Look for “" + airplayBackend.deviceName + "” in Control Center"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.025
        }
        // Renaming has no on-screen setting (see AirPlayBackend::
        // deviceNameFilePath()'s comment for why) — showing the exact path
        // here means a user doesn't have to dig through the wiki just to
        // find where the file goes. Wrapped/width-bounded since a real
        // absolute path can run long on a Pi.
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.sw * 0.76875
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            text: "Rename: " + airplayBackend.deviceNameFilePath()
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0208333
            opacity: 0.7
        }
        // Now that the module is just "AirPlay" (not "AirPlay Audio") in the
        // module list and AppBar title, this is the one place a user
        // actually reaches expecting to send it video/screen mirroring —
        // worth being explicit here, not just in the settings description
        // and wiki.
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Audio only — no video or screen mirroring"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0208333
            opacity: 0.7
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Please check the wiki for additional instructions"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0208333
            opacity: 0.7
        }
    }

    // --- Error state: shairport-sync never got a chance to work ------------------------
    // Distinct from the idle state above — "nobody's connected yet" is normal;
    // this is "something's actually wrong". See
    // AirPlayBackend::handleProcessFinished()'s comment for what sets it.
    Column {
        anchors.centerIn: parent
        width: root.sw * 0.76875
        spacing: root.sh * 0.0333333
        visible: nowPlaying.hasError

        Text {
            text: "AirPlay receiver couldn't start"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.05
        }
        Text {
            text: airplayBackend.receiverError
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.0291667
        }
        Text {
            text: "Please check the wiki for additional instructions"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.0333333
        }
    }

    // --- Connected state: album art + track metadata ------------------------------------
    Row {
        anchors.centerIn: parent
        spacing: root.sw * 0.03
        visible: nowPlaying.connected

        Image {
            id: artwork
            width: root.sh * 0.4
            height: root.sh * 0.4
            visible: nowPlaying.albumArtEnabled
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            source: airplayBackend.artworkPath.length > 0
                    ? "file://" + airplayBackend.artworkPath
                    : ""
        }

        // Shown instead of artwork when the "Album Art" setting is off — a
        // theme-colored square (the same footprint artwork would occupy)
        // with a dedicated cassette glyph (assets/images/cassette.svg,
        // separate from the app's own logo.svg — that one's a single fused
        // path not meant to be pulled apart) rendered in the *inverse*
        // color on top, so it reads as if it were cut out of the square.
        // Originally this was going to be a per-service logo (Spotify/Apple
        // Music/Podcasts/etc.) picked by whichever app was actually
        // streaming, but AirPlay's metadata protocol has no field for that
        // at all — confirmed by checking shairport-sync's own source
        // directly, not assumed. It's source-agnostic by design: the
        // receiver gets a title/artist/album, never which app sent it. A
        // generic placeholder is what's actually achievable here.
        Item {
            id: genericArt
            width: root.sh * 0.4
            height: root.sh * 0.4
            visible: !nowPlaying.albumArtEnabled

            Rectangle {
                anchors.fill: parent
                color: root.accentColor
            }

            // Static glyph — assets/images/cassette.svg already has the
            // reel-hub/window cutouts baked in as real holes (an evenodd
            // path, same trick the app's own logo.svg badge uses), so this
            // is just a tinted image, no live compositing needed.
            Image {
                id: cassetteImage
                visible: false
                anchors.centerIn: parent
                // Padding around the glyph — filling the square edge-to-edge
                // read as oversized/heavy; leaving room shows the badge
                // color as a visible border, like the rest of this app's
                // icon badges.
                width: parent.width * 0.7
                height: width * (163.64 / 300)
                // Without this, the SVG rasterizes once at its own intrinsic
                // size and that bitmap gets stretched up to fill the display
                // size, which reads as blurry. Setting sourceSize to the
                // actual on-screen size makes Qt's SVG renderer rasterize at
                // the right resolution directly.
                sourceSize.width: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                source: "../assets/images/cassette.svg"
            }
            MultiEffect {
                anchors.fill: cassetteImage
                source: cassetteImage
                colorization: 1.0
                colorizationColor: root.surfaceColor
            }
        }

        Column {
            spacing: root.sh * 0.015
            anchors.verticalCenter: parent.verticalCenter
            width: root.sw * 0.35

            MarqueeText {
                text: airplayBackend.trackTitle
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0583333
                width: parent.width
            }
            MarqueeText {
                text: airplayBackend.artist
                color: root.secondaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0416667
                width: parent.width
            }
            MarqueeText {
                text: airplayBackend.album
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0375
                width: parent.width
            }
            MarqueeText {
                visible: airplayBackend.senderName.length > 0
                text: "Streaming from " + airplayBackend.senderName
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667
                width: parent.width
            }
        }
    }

    Text {
        text: root.hints.back + ":BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }

    // Only run shairport-sync (and, unless "Allow Screen Saver" is on,
    // suppress the screen saver) while this screen is actually open — see
    // AirPlayBackend for why nqptp is not managed the same way.
    Component.onCompleted: {
        refreshAlbumArtSetting()
        refreshAllowScreenSaverSetting() // sets idleTracker.mpvActive itself
        airplayBackend.startReceiver()
    }
    Component.onDestruction: {
        airplayBackend.stopReceiver()
        idleTracker.mpvActive = false
        idleTracker.resetActivity()
    }
}
