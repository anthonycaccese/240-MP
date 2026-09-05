import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    readonly property string moduleName:
        appCore ? (appCore.get_module_info(moduleId).name || "") : ""
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()
    signal exitModule()

    property int currentSlotIndex: -1
    property int consecutiveFailures: 0

    property string moduleId: navParams.moduleId || ""
    property bool offAir: false
    property bool tuning: false
    property bool filler: false
    property bool rebuildTried: false
    property bool rebuilding: false
    property string rebuildNote: ""
    property string fillerLogo: ""
    property string offAirText: ""

    property var dial: []
    property bool switchingTo: false
    property int pendingChannel: -1
    property real tuneAskedAt: 0

    property int overrunTicks: 0

    property bool recovering: false

    property string leavingTo: ""

    readonly property string weatherView: "WeatherChannel.qml"

    property var dialRows: []

    function buildDial() {
        dialRows = virtualChannelsBackend.list_channels()
        var d = []
        for (var i = 0; i < dialRows.length; i++) d.push(dialRows[i].number)
        dial = d
    }

    function rowForChannel(num) {
        for (var i = 0; i < dialRows.length; i++)
            if (dialRows[i].number === num) return dialRows[i]
        return null
    }

    function viewForChannel(num) {
        var row = rowForChannel(num)
        if (!row || !row.special || row.special === "") return ""
        return row.special === "weather" ? weatherView : "Guide.qml"
    }

    function leaveFor(path) {
        virtualChannelsBackend.release_tuner()
        if (offAir || tuning || filler) {
            navigateTo(path, {}, { fromPlayer: true })
        } else {
            leavingTo = path
            mpvController.stop()
        }
    }

    function nameForChannel(num) {
        var row = rowForChannel(num)
        return row ? row.name : ("Channel " + num)
    }

    readonly property int surfSettleMs: 350

    Timer {
        id: surfSettle
        interval: playerRoot.surfSettleMs
        repeat: false
        onTriggered: playerRoot.applyChannelChange()
    }

    function changeChannel(direction) {
        if (dial.length === 0) buildDial()
        if (dial.length === 0) return

        var from = pendingChannel >= 0 ? pendingChannel : channelNumber
        var at = dial.indexOf(from)
        if (at < 0) at = 0
        var next = dial[(at + direction + dial.length) % dial.length]
        if (next === from) return

        var view = viewForChannel(next)
        if (view !== "") { leaveFor(view); return }

        pendingChannel = next
        channelName = nameForChannel(next)
        raiseLocalBanner(next, channelName)

        if (offAir || tuning || filler) {
            surfSettle.restart()
        } else if (!switchingTo) {
            switchingTo = true
            mpvController.stop()
        }
    }

    function applyChannelChange() {
        surfSettle.stop()
        var next = pendingChannel
        pendingChannel = -1
        switchingTo = false
        if (next < 0) { beginTune(); return }
        channelNumber = next
        channelName = nameForChannel(next)
        consecutiveFailures = 0
        rebuildTried = false
        rebuilding = false
        raiseBanner(next, channelName)
        raiseLocalBanner(next, channelName)
        beginTune()
    }

    // Title-safe: broadcast keeps text and logos inside the middle 80% of the
    // picture, because a tube loses the rest to overscan. Eight per cent put the
    // logo's own edge at 92% across and down -- outside that, in the band only
    // non-essential graphics are allowed in -- and a set overscanning ten per
    // cent simply cut it off.
    readonly property real logoInset: 0.10

    property int masterVolume: 100

    // What fills the screen between one channel and the next. Read rather than
    // bound so it cannot be reached for while the view is being torn down.
    property string tuningScreen: "Card"
    readonly property bool waiting: tuning && !rebuilding
    readonly property bool showSnow:  waiting && tuningScreen === "Static"
    readonly property bool showBlank: waiting && tuningScreen === "Black"

    function loadTuningScreen() {
        var v = appCore.get_setting(moduleId, "tuning_screen")
        tuningScreen = (v === undefined || v === null || String(v) === "")
                       ? "Card" : String(v)
    }

    function loadVolume() {
        var v = appCore.get_setting(moduleId, "volume")
        if (v !== undefined && v !== null && String(v) !== "")
            masterVolume = Math.max(0, Math.min(100, parseInt(v)))
        mpvController.setVolume(masterVolume)
    }

    property bool volumeEchoSeen: false

    function toggleOn(key, fallback) {
        var v = appCore.get_setting(moduleId, key)
        if (v === undefined || v === null || v === "") return fallback
        if (typeof v === "string") return v.toUpperCase() === "ON"
        return !!v
    }

    function volumeArgs() {
        var args = ["--volume=" + masterVolume]
        if (toggleOn("normalize_volume", false))
            args.push("--af=loudnorm=I=-16:LRA=11:TP=-1.5")
        return args
    }

    // How much of the frame survives the crop on each axis, written in whichever
    // names the filter at hand gives the picture: overlay calls them W and H,
    // scale2ref calls its reference iw and ih. Both are 1 when nothing is
    // cropped, and every expression built on them collapses back to its simple
    // form.
    function cropFactors(pictureWidth, pictureHeight) {
        if (!mpvController.cropActive()) return { width: "1", height: "1" }
        var d = (root.sh > 0 ? root.sw / root.sh : 1.5).toFixed(6)
        return {
            width:  "min(1\\," + d + "*" + pictureHeight + "/" + pictureWidth + ")",
            height: "min(1\\," + pictureWidth + "/(" + pictureHeight + "*" + d + "))"
        }
    }

    // Where the logo sits, measured against what the viewer can actually see.
    //
    // The filter draws into the video frame, and mpv then scales that frame to
    // fill the screen -- cropping the overflowing edges when Auto Crop is on.
    // Anything inset from the frame is dragged toward the edge by that crop, by
    // an amount that depends on the shape of whatever is playing: a 16:9 stream
    // on this 3:2 output loses 7.8% of its width each side, which put a logo
    // inset 10% at 97.4% across the screen, while 4:3 material sat correctly at
    // 90%. Same setting, different place, which is what made it look arbitrary.
    // Taking the inset from the visible rectangle instead holds every shape at
    // the 90% the setting asks for.
    function overlayAt(ox, oy) {
        var visible = cropFactors("W", "H")
        return "W*(0.5+0.5*" + visible.width + ")-w-(W*" + playerRoot.logoInset + "*" + visible.width + ")"
             + "+(W*" + ox.toFixed(4) + "*" + visible.width + ")"
             + ":H*(0.5+0.5*" + visible.height + ")-h-(H*" + playerRoot.logoInset + "*" + visible.height + ")"
             + "+(H*" + oy.toFixed(4) + "*" + visible.height + ")"
    }

    function logoFile() {
        var file = virtualChannelsBackend.channel_logo(channelNumber) || ""
        if (file === "") file = appCore.get_setting(moduleId, "logo.file") || ""
        return file
    }

    function logoCarriesSeek(startSeconds) {
        var seek = Number(startSeconds)
        if (!isFinite(seek) || seek < 1) return false
        return logoIsAnimated()
    }

    function logoIsAnimated() { return logoFile().toLowerCase().endsWith(".gif") }

    function logoPercent(key, fallback) {
        var v = parseFloat(appCore.get_setting(moduleId, "logo." + key))
        return (isNaN(v) ? fallback : v) / 100.0
    }

    // How much of the picture a logo is allowed to cover, whatever its shape.
    //
    // Sizing by width alone made the setting mean something different for every
    // logo: at 12% a square mark stood 18% of the picture tall and a wordmark six
    // times as wide as it is high stood 2.8% -- the same number producing a
    // sixfold difference in what the viewer sees. Matching area instead makes the
    // setting mean weight on screen: a square logo is exactly as before, and
    // every other shape covers the same ground.
    //
    // w = W*size*sqrt(aspect) and h = W*size/sqrt(aspect) multiply to (W*size)^2
    // for any aspect. The cap stops a very long wordmark from becoming a banner.
    readonly property real logoMaxWidth: 0.22

    function sizedOverLogoGraph(source, opacity, size, offsetX, offsetY, endWithPicture) {
        // Scaled by the same crop factor as the position, and for the same
        // reason: the size is a share of the frame, and the crop then magnifies
        // what is left of that frame to fill the screen. Uncorrected, one
        // setting drew a logo covering 25.4% of the screen under a 16:9 episode
        // and 21.4% under a 4:3 advert, so it grew at every break.
        var width = "min(iw*" + size.toFixed(4) + "*sqrt(mdar)\\,iw*"
                    + logoMaxWidth.toFixed(2) + ")*" + cropFactors("iw", "ih").width
        return source
             + ",format=rgba,colorchannelmixer=aa=" + opacity.toFixed(3) + "[logo];"
             + "[logo][vid1]scale2ref=w=" + width + ":h=ow/mdar[sized][picture];"
             + "[picture][sized]overlay=" + overlayAt(offsetX, offsetY)
             // eval=frame because a channel hands mpv a playlist and the items
             // are not all the same shape: a 720p episode, then a 480p advert.
             // Evaluated once at init, the position would be right for whichever
             // item happened to open the run and wrong for the rest.
             + ":eval=frame"
             + (endWithPicture ? ":shortest=1" : "") + "[vo]"
    }

    function logoArgs(startSeconds) {
        var file = logoFile()
        if (file === "") return []
        var path = virtualChannelsBackend.logo_path(file)
        if (!path || path === "") return []

        var size    = logoPercent("size", 12)
        var opacity = logoPercent("opacity", 70)
        var offsetX = logoPercent("offset_x", 0)
        var offsetY = logoPercent("offset_y", 0)

        if (!logoIsAnimated())
            return ["--lavfi-complex=" + sizedOverLogoGraph("movie=" + path, opacity, size,
                                                            offsetX, offsetY, false)]

        var seek = Number(startSeconds)
        var startWherePictureStarts = (isFinite(seek) && seek >= 1)
                                      ? ("+" + seek.toFixed(3) + "/TB") : ""
        // shortest=1: a looping logo never ends, and an overlay whose second
        // input never ends stops mpv ever reaching the end of the programme.
        return ["--lavfi-complex="
                + sizedOverLogoGraph("movie=" + path + ":loop=0,setpts=N/FRAME_RATE/TB"
                                     + startWherePictureStarts,
                                     opacity, size, offsetX, offsetY, true)]
    }

    // Said the moment the button is pressed rather than when the picture
    // arrives. mpv cannot draw it yet -- it is stopped, or has not started --
    // and on a fast surf that wait is the whole of it.
    function raiseLocalBanner(number, name) {
        var wantNumber = appCore.get_setting(moduleId, "banner.number") !== "OFF"
        var wantName   = appCore.get_setting(moduleId, "banner.name")   !== "OFF"
        if (!wantNumber && !wantName) return
        channelBanner.seconds = bannerSeconds()
        channelBanner.offsetX = bannerOffset("banner.offset_x")
        channelBanner.offsetY = bannerOffset("banner.offset_y")
        channelBanner.show(wantNumber ? String(number) : "",
                           wantName ? String(name) : "")
    }

    function bannerSeconds() {
        var secs = parseFloat(appCore.get_setting(moduleId, "banner.seconds"))
        return isNaN(secs) ? 1.5 : secs
    }

    function bannerOffset(key) {
        var v = parseInt(appCore.get_setting(moduleId, key))
        return (isNaN(v) ? 0 : v) / 100.0
    }

    function raiseBanner(number, name) {
        var wantNumber = appCore.get_setting(moduleId, "banner.number") !== "OFF"
        var wantName   = appCore.get_setting(moduleId, "banner.name")   !== "OFF"
        if (!wantNumber && !wantName) return

        var secs = parseFloat(appCore.get_setting(moduleId, "banner.seconds"))
        var ox   = parseInt(appCore.get_setting(moduleId, "banner.offset_x"))
        var oy   = parseInt(appCore.get_setting(moduleId, "banner.offset_y"))

        mpvController.showChannelOsd({
            "number":  wantNumber ? String(number) : "",
            "name":    wantName ? String(name).toUpperCase() : "",
            "seconds": isNaN(secs) ? 1.5 : secs,
            "offsetX": (isNaN(ox) ? 0 : ox) / 100.0,
            "offsetY": (isNaN(oy) ? 0 : oy) / 100.0,
            "font":    root.globalFont,
            "color":   root.primaryColor
        })
    }

    function beginTune() {
        loadTuningScreen()
        tuneAskedAt = Date.now()
        rebuildNote = ""
        filler = false
        fillerTimer.stop()
        recovering = false
        overrunTicks = 0
        offAir = false
        tuning = true
        tuneSettle.restart()
    }

    Timer {
        id: tuneSettle
        interval: 120
        repeat: false
        onTriggered: playerRoot.apply(virtualChannelsBackend.tune(playerRoot.channelNumber))
    }

    focus: true

    Rectangle {
        anchors.fill: parent
        color: playerRoot.showSnow || playerRoot.showBlank ? "black" : root.surfaceColor
    }

    SnowField {
        anchors.fill: parent
        visible: playerRoot.showSnow
        running: playerRoot.showSnow
    }

    // -----------------------------------------------------------------------
    // Playback
    // -----------------------------------------------------------------------

    Timer {
        id: fillerTimer
        repeat: false
        onTriggered: {
            if (!playerRoot.filler) return
            playerRoot.filler = false
            playerRoot.beginTune()
        }
    }

    readonly property int overrunSlackMs: 15000

    Timer {
        id: overrunGuard
        interval: 5000
        repeat: true
        running: !playerRoot.offAir && !playerRoot.tuning && !playerRoot.filler
                 && !playerRoot.switchingTo && playerRoot.leavingTo === ""
                 && !playerRoot.recovering
        onTriggered: {
            var dur = mpvController.duration
            var pos = mpvController.position
            if (dur <= 0 || pos <= dur + playerRoot.overrunSlackMs) {
                playerRoot.overrunTicks = 0
                return
            }
            if (++playerRoot.overrunTicks < 2) return
            playerRoot.overrunTicks = 0
            console.log("[channels] channel " + playerRoot.channelNumber
                        + ": playback ran " + Math.round((pos - dur) / 1000)
                        + "s past the end of its file; moving on")
            playerRoot.recovering = true
            mpvController.stop()
        }
    }

    function apply(descriptor) {
        if (descriptor && descriptor.filler) {
            tuning = false
            offAir = false
            filler = true
            fillerLogo = descriptor.logo || ""
            channelName = descriptor.title || channelName
            var secs = Number(descriptor.seconds)
            fillerTimer.interval = (isFinite(secs) && secs > 0.5)
                                   ? Math.min(secs, 3600) * 1000 : 1000
            fillerTimer.restart()
            return
        }
        filler = false
        fillerTimer.stop()

        if (descriptor && descriptor.needsRegeneration && !rebuildTried) {
            rebuildTried = true
            rebuilding = true
            offAir = false
            tuning = true
            virtualChannelsBackend.regenerate(channelNumber)
            return
        }

        if (!descriptor) {
            tuning = false
            offAir = true
            offAirText = "Off air"
            return
        }

        if (descriptor.pending) {
            offAir = false
            tuning = true
            return
        }

        if (!descriptor.play) {
            tuning = false
            offAir = true
            offAirText = descriptor.message ? descriptor.message : "Off air"
            return
        }

        tuning = false
        offAir = false
        recovering = false
        overrunTicks = 0
        rebuildTried = false
        currentSlotIndex = descriptor.slotIndex

        if (tuneAskedAt > 0) {
            console.log("[channels] channel " + channelNumber + ": "
                        + Math.round(Date.now() - tuneAskedAt)
                        + "ms from asking to handing over to mpv")
            tuneAskedAt = 0
        }

        volumeEchoSeen = false
        mpvController.loadAndPlay(
            descriptor.url,
            descriptor.startSeconds,
            0,
            -1,
            [],
            [],
            false,
            -1,
            0.0,
            descriptor.plexToken || "",
            false,
            "",
            false,
            [],
            0.0,
            false,
            playerRoot.logoArgs(descriptor.startSeconds).concat(playerRoot.volumeArgs()),
            descriptor.jellyfinToken || "",
            playerRoot.extraUrlsFor(descriptor)
        )
    }

    function extraUrlsFor(descriptor) {
        if (logoCarriesSeek(descriptor.startSeconds)) return []
        return descriptor.extraUrls || []
    }

    Component.onDestruction: virtualChannelsBackend.release_tuner()

    Component.onCompleted: {
        if (channelNumber < 0) { exitModule(); return }
        loadVolume()
        loadTuningScreen()
        buildDial()
        // Arriving on a channel announces it, the same as changing to one.
        // Reaching a channel from the guide comes through here rather than
        // through applyChannelChange, so without this the banner only ever
        // appeared while surfing the dial -- which is to say almost never.
        raiseBanner(channelNumber, channelName)
        raiseLocalBanner(channelNumber, channelName)
        beginTune()
    }

    Connections {
        target: virtualChannelsBackend

        function onGenerationProgress(ch, done, total) {
            if (!playerRoot.rebuilding || ch !== playerRoot.channelNumber) return
            playerRoot.rebuildNote = total > 0
                ? "Rebuilding schedule… " + done + " of " + total
                : "Rebuilding schedule…"
        }

        function onGenerationFinished(ch, ok, message) {
            if (!playerRoot.rebuilding || ch !== playerRoot.channelNumber) return
            playerRoot.rebuilding = false
            playerRoot.rebuildNote = ""
            if (ok) {
                playerRoot.beginTune()
            } else {
                if (virtualChannelsBackend.is_generating())
                    playerRoot.rebuildTried = false
                playerRoot.tuning = false
                playerRoot.offAir = true
                playerRoot.offAirText = message ? message : "Off air"
            }
        }

        function onPlayDescriptorReady(descriptor) {
            playerRoot.apply(descriptor)
        }
    }

    Connections {
        target: mpvController

        function onVolumeChanged(percent) {
            if (!playerRoot.volumeEchoSeen) {
                playerRoot.volumeEchoSeen = true
                if (percent !== playerRoot.masterVolume)
                    mpvController.setVolume(playerRoot.masterVolume)
                return
            }
            if (percent === playerRoot.masterVolume) return
            playerRoot.masterVolume = percent
            appCore.save_setting(playerRoot.moduleId, "volume", String(percent))
        }

        // A stream that stops arriving -- a transcode session reaped, a server
        // that went away -- leaves mpv connected and unpaused with nothing to
        // decode. Nothing about that ends playback, so a channel left to itself
        // would hold one frame for as long as it was on. The schedule has moved
        // on regardless, so give up on the item and ask what is on now.
        function onPlaybackStalled(silentSeconds) {
            if (playerRoot.recovering || playerRoot.switchingTo
                || playerRoot.leavingTo !== "" || playerRoot.offAir
                || playerRoot.tuning || playerRoot.filler) return
            console.log("[channels] channel " + playerRoot.channelNumber
                        + ": nothing played for " + silentSeconds + "s; moving on")
            playerRoot.recovering = true
            mpvController.forceStop()
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            playerRoot.tuneAskedAt = Date.now()
            if (playerRoot.leavingTo !== "") {
                var dest = playerRoot.leavingTo
                playerRoot.leavingTo = ""
                navigateTo(dest, {}, { fromPlayer: true })
                return
            }
            if (playerRoot.switchingTo) {
                playerRoot.switchingTo = false
                playerRoot.offAir = false
                playerRoot.tuning = true
                surfSettle.restart()
                return
            }
            if (playerRoot.recovering) {
                playerRoot.recovering = false
                playerRoot.consecutiveFailures = 0
                playerRoot.apply(virtualChannelsBackend.after_playback(
                                     playerRoot.channelNumber, "eof",
                                     playerRoot.currentSlotIndex, 0))
                return
            }

            if (reason === "stopped") {
                virtualChannelsBackend.release_tuner()
                exitModule()
                return
            }

            if (reason === "failed")
                playerRoot.consecutiveFailures += 1
            else
                playerRoot.consecutiveFailures = 0

            apply(virtualChannelsBackend.after_playback(
                      playerRoot.channelNumber,
                      reason,
                      playerRoot.currentSlotIndex,
                      playerRoot.consecutiveFailures))
        }
    }

    Timer {
        interval: 10000
        running: playerRoot.offAir
        repeat: true
        onTriggered: playerRoot.apply(virtualChannelsBackend.tune(playerRoot.channelNumber))
    }

    // -----------------------------------------------------------------------
    // Input
    // -----------------------------------------------------------------------

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_ChannelUp || event.key === Qt.Key_PageUp) {
            changeChannel(1); event.accepted = true; return
        }
        if (event.key === Qt.Key_ChannelDown || event.key === Qt.Key_PageDown) {
            changeChannel(-1); event.accepted = true; return
        }

        if (offAir || tuning || filler) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                virtualChannelsBackend.release_tuner()
                exitModule()
                event.accepted = true
            }
            return
        }

        if (event.key === Qt.Key_Escape)             mpvController.sendKey("ESC")
        else if (event.key === Qt.Key_Backspace ||
                 event.key === Qt.Key_Back)          mpvController.sendKey("BS")
        else if (event.key === Qt.Key_Up)            mpvController.sendKey("UP")
        else if (event.key === Qt.Key_Down)          mpvController.sendKey("DOWN")
        else if (event.key === Qt.Key_Left)          mpvController.sendKey("LEFT")
        else if (event.key === Qt.Key_Right)         mpvController.sendKey("RIGHT")
        else if (event.key === Qt.Key_Space)         mpvController.sendKey("SPACE")
        else if (event.key === Qt.Key_Return ||
                 event.key === Qt.Key_Enter)         mpvController.sendKey("ENTER")
        else return
        event.accepted = true
    }

    // -----------------------------------------------------------------------
    // Off-air / tuning screen
    // -----------------------------------------------------------------------

    ChannelBanner {
        id: channelBanner
        anchors.fill: parent
        textColor: root.primaryColor
        fontFamily: root.globalFont
    }

    AppBar {
        visible: (playerRoot.offAir || playerRoot.tuning)
                 && !playerRoot.showSnow && !playerRoot.showBlank
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: playerRoot.moduleIcon
        title: playerRoot.moduleName
    }

    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333
        visible: (playerRoot.offAir || playerRoot.tuning || playerRoot.filler)
                 && !playerRoot.showSnow && !playerRoot.showBlank

        AnimatedImage {
            visible: playerRoot.filler && playerRoot.fillerLogo !== ""
            source: playerRoot.filler ? playerRoot.fillerLogo : ""
            playing: visible
            cache: false
            fillMode: Image.PreserveAspectFit
            readonly property real aspect: sourceSize.width > 0
                                           ? sourceSize.height / sourceSize.width : 1
            width: root.sw * 0.22
            height: width * aspect
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: playerRoot.channelName
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.05
        }
        Text {
            visible: !playerRoot.filler
            text: playerRoot.rebuilding
                  ? (playerRoot.rebuildNote !== "" ? playerRoot.rebuildNote
                                                   : "Rebuilding schedule…")
                  : (playerRoot.tuning ? "Tuning…" : playerRoot.offAirText)
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333
        }
    }

    Text {
        visible: (playerRoot.offAir || playerRoot.tuning)
                 && !playerRoot.showSnow && !playerRoot.showBlank
        text: root.hints.back + ":BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
