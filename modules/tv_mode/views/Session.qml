import QtQuick

// The TV session: one long-lived mpv, driven over IPC.
//
// Unlike every other Player view in 240-MP, this one does NOT return to the menu
// when a file ends — that's the whole point. mpv stays up with keep-open, and
// `fileEnded` is the cue to roll the next episode on the current channel. The
// only thing that leaves is the viewer pressing back, which ends the session and
// waits for `playbackEnded` (the mpv process actually exiting) before popping.
//
// Nothing here is visible on the Pi: mpv owns the framebuffer for the duration
// via the DRM/VT hand-off. This view exists to hold focus and route keys.
FocusScope {
    id: sessionRoot

    property var navParams: ({})

    signal goBack()

    property int  startIndex:   navParams.startIndex !== undefined ? navParams.startIndex : 0
    property bool openGuideOnStart: navParams.openGuide === true
    property bool sessionEnded: false
    property string currentPath: ""
    property int  lastPositionMs: 0
    // Channel banner held back until a bridge switch actually cuts over.
    property var    pendingBanner: null
    // Remembered so ► can re-show the banner without re-tuning the channel.
    property int    lastNumber: 0
    property string lastName:   ""

    focus: true

    Keys.onPressed: function(event) {
        if (sessionEnded) { event.accepted = true; return }

        // Innermost overlay first. The stack is guide -> settings -> overscan,
        // with the episode list a sibling of settings; each one stays up
        // underneath the next so BACK unwinds one level at a time.
        if (transportOpen) {
            transportKey(event)
            event.accepted = true
            return
        }

        if (calibrateOpen) {
            calibrateKey(event)
            event.accepted = true
            return
        }

        if (orderOpen) {
            channelOrderKey(event)
            event.accepted = true
            return
        }

        if (settingsOpen) {
            settingsKey(event)
            event.accepted = true
            return
        }

        if (episodesOpen) {
            episodesKey(event)
            event.accepted = true
            return
        }

        if (guideOpen) {
            guideKey(event)
            event.accepted = true
            return
        }

        // Discrete actions fire once per press, never on auto-repeat. An IR
        // press through the Flirc that lingers a moment — or a remote sending
        // repeat frames — otherwise delivers channelUp twice and reads as the
        // dial skipping a channel.
        //
        // Seek is deliberately exempt: repeating while held is how you scrub.
        // Same split InputManager already makes for volume keys vs. the rest.
        if (event.isAutoRepeat
                && event.key !== Qt.Key_Comma
                && event.key !== Qt.Key_Period) {
            event.accepted = true
            return
        }

        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // Transport controls. Seeking has to be reachable with nothing but
            // the six baseline keys (up/down/left/right/enter/back), so it lives
            // behind ENTER rather than only on , and . — those stay as an
            // accelerator for keyboards that have them.
            openTransport()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            leaveSession()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            rememberPosition()
            play(tvModeBackend.channelUp(), true)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            rememberPosition()
            play(tvModeBackend.channelDown(), true)
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            // Flip to the previously watched channel. Does nothing until you
            // have changed channel at least once.
            if (tvModeBackend.hasLastChannel()) {
                rememberPosition()
                play(tvModeBackend.lastChannel(), true)
            }
            event.accepted = true
        } else if (event.key === Qt.Key_F10) {
            // SLEEP. The TV acts on this press too, running its own countdown —
            // this mirrors the same ladder so the Pi shuts down in step with it.
            cycleSleep()
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            // The guide subsumes the old "re-show banner" function: it says what
            // is on, and there is no spare button on the remote for a GUIDE key.
            openGuide()
            event.accepted = true
        } else if (event.key === Qt.Key_Comma) {
            seekBy(-10)
            event.accepted = true
        } else if (event.key === Qt.Key_Period) {
            seekBy(10)
            event.accepted = true
        }
    }

    // -- seeking -------------------------------------------------------------
    // During playback the arrows are all spoken for (channel / last channel /
    // guide), so the remote-reachable way to scrub is the transport panel on
    // ENTER; , and . are an accelerator for keyboards that have them. mpv runs
    // with no OSC and no key bindings in session mode, so this is the only path
    // to a seek; MpvController::seekTo takes an ABSOLUTE position.
    //
    // lastPositionMs is advanced optimistically because mpv's position events
    // lag a few hundred ms. Without that, a quick double-tap would compute both
    // seeks from the same stale base and the second would undo the first.
    function seekBy(deltaSeconds) {
        if (currentPath === "") return
        var target = Math.max(0, lastPositionMs + deltaSeconds * 1000)
        lastPositionMs = target
        mpvController.seekTo(target)
        // The transport panel already shows the position; a second readout on
        // top of it would just fight for the same corner.
        if (!transportOpen)
            showMessage((deltaSeconds < 0 ? "<< " : ">> ") + formatClock(target))
    }

    // -- transport ------------------------------------------------------------
    // A seek bar reachable from the baseline remote alone: ENTER opens it,
    // LEFT/RIGHT scrub, UP/DOWN jump a minute, BACK closes. Without this, seek
    // would exist only on keys a plain USB remote does not have.
    property bool transportOpen: false

    function openTransport() {
        if (currentPath === "") return          // nothing playing, or an ad
        transportOpen = true
        drawTransport()
    }

    function closeTransport() {
        transportOpen = false
        mpvController.clearOverlay(tvModeBackend.overlayIdSettings())
    }

    function drawTransport() {
        mpvController.setOverlay(tvModeBackend.overlayIdSettings(),
                                 tvModeBackend.settingsAss(guideColors(), "TRANSPORT",
                                                           ["POSITION"],
                                                           [formatClock(lastPositionMs)],
                                                           0, 0, 1,
                                                           "L/R: 10 SEC   UP/DN: 1 MIN   BACK: CLOSE",
                                                           true),
                                 tvModeBackend.canvasWidth(),
                                 tvModeBackend.canvasHeight())
    }

    function transportKey(event) {
        if (event.key === Qt.Key_Left) {
            seekBy(-10); drawTransport()
        } else if (event.key === Qt.Key_Right) {
            seekBy(10);  drawTransport()
        } else if (event.key === Qt.Key_Down) {
            seekBy(-60); drawTransport()
        } else if (event.key === Qt.Key_Up) {
            seekBy(60);  drawTransport()
        } else if (!event.isAutoRepeat
                   && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                       || event.key === Qt.Key_Back || event.key === Qt.Key_Return
                       || event.key === Qt.Key_Enter)) {
            closeTransport()
        }
    }

    function formatClock(ms) {
        var t = Math.floor(ms / 1000)
        var h = Math.floor(t / 3600)
        var m = Math.floor((t % 3600) / 60)
        return (h > 0 ? h + ":" + pad2(m) : String(m)) + ":" + pad2(t % 60)
    }

    // -- TV guide -----------------------------------------------------------
    // Drawn over the running show as an ASS overlay: on EGLFS mpv owns the
    // framebuffer, so a Qt view would mean tearing the session down and
    // rebuilding it. Styling follows 240-MP's active colour scheme.
    property bool guideOpen:    false
    property int  guideRow:     0
    property int  guideCol:     0
    property int  guideFirstRow: 0
    readonly property int guideVisibleRows: 6

    function guideColors() {
        return {
            "primary":   root.primaryColor,
            "secondary": root.secondaryColor,
            "tertiary":  root.tertiaryColor,
            "surface":   root.surfaceColor,
            "accent":    root.accentColor,
            "font":      root.globalFont
        }
    }

    function drawGuide() {
        mpvController.setOverlay(tvModeBackend.overlayIdGuide(),
                                 tvModeBackend.guideAss(guideColors(), guideRow,
                                                        guideCol, guideFirstRow,
                                                        guideVisibleRows),
                                 tvModeBackend.canvasWidth(),
                                 tvModeBackend.canvasHeight())
    }

    // Remembered so the settings bar knows which channel its toggle applies to.
    property int guideLastChannelRow: 0

    function saveChannelOrders() {
        var map = {}
        var list = tvModeBackend.channels()
        for (var i = 0; i < list.length; i++)
            map[String(list[i].number)] = tvModeBackend.channelOrder(i)
        appCore.save_setting(moduleRoot.moduleId, "channel_orders", map)
    }

    function openGuide() {
        var cur = tvModeBackend.currentChannel()
        guideRow = (cur && cur.index !== undefined) ? cur.index : 0
        guideCol = 0
        guideLastChannelRow = guideRow
        scrollGuideIntoView()
        guideOpen = true
        // The banner would sit on top of the grid; the guide says it all.
        bannerTimer.stop()
        mpvController.clearOverlay(tvModeBackend.overlayIdChannel())
        drawGuide()
    }

    function closeGuide() {
        guideOpen = false
        mpvController.clearOverlay(tvModeBackend.overlayIdGuide())
    }

    function scrollGuideIntoView() {
        if (guideRow >= tvModeBackend.channelCount()) return   // settings bar
        if (guideRow < guideFirstRow)
            guideFirstRow = guideRow
        else if (guideRow >= guideFirstRow + guideVisibleRows)
            guideFirstRow = guideRow - guideVisibleRows + 1
        if (guideFirstRow < 0) guideFirstRow = 0
    }

    function guideKey(event) {
        // Arrows repeat so a long lineup can be scrolled by holding; select and
        // back must not, or one press would open and immediately act twice.
        if (event.isAutoRepeat
                && event.key !== Qt.Key_Up && event.key !== Qt.Key_Down
                && event.key !== Qt.Key_Left && event.key !== Qt.Key_Right)
            return
        var n = tvModeBackend.channelCount()
        // Row n is the settings bar along the bottom, past the last channel.
        if (event.key === Qt.Key_Up) {
            guideRow = (guideRow <= 0) ? n : guideRow - 1
            if (guideRow < n) guideLastChannelRow = guideRow
            scrollGuideIntoView(); drawGuide()
        } else if (event.key === Qt.Key_Down) {
            guideRow = (guideRow >= n) ? 0 : guideRow + 1
            if (guideRow < n) guideLastChannelRow = guideRow
            scrollGuideIntoView(); drawGuide()
        } else if (event.key === Qt.Key_Left) {
            // -1 is the channel-name cell, left of the first slot: the way in
            // to the channel's full episode list.
            if (guideCol > -1) { guideCol--; drawGuide() }
        } else if (event.key === Qt.Key_Right) {
            if (guideCol < tvModeBackend.guideColumns() - 1) { guideCol++; drawGuide() }
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (guideRow >= tvModeBackend.channelCount()) {
                // The bottom bar opens the settings page rather than changing
                // anything itself — nothing here is one press from the guide.
                settingsRow = 0
                openSettings()
            } else if (guideCol < 0) {
                openEpisodeList(guideRow)
            } else {
                // Save where we were first, exactly as a channel change does —
                // must happen before playGuideSelection() re-points the lineup,
                // because rememberPosition() records against the CURRENT
                // channel. Without it, jumping to a "NEXT"/"THEN" slot loses the
                // episode you were part-way through.
                rememberPosition()
                var req = tvModeBackend.playGuideSelection(guideRow, guideCol)
                closeGuide()
                play(req, true)
            }
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                   || event.key === Qt.Key_Back) {
            closeGuide()
        }
    }

    // -- settings page -------------------------------------------------------
    // Opened from the guide's SETTINGS row, so every option here has to be
    // navigated to. Rows are declared in QML and drawn by the backend, which
    // keeps "what the options are" next to the code that acts on them.
    property bool settingsOpen: false
    property int  settingsRow:  0
    readonly property var settingsRows: ["channel_order", "episode_order",
                                         "ads", "sleep", "overscan"]

    function settingsLabels() {
        return ["CHANNEL ORDER", "ALL CHANNELS ORDER", "COMMERCIALS ON SELECTION",
                "SLEEP TIMER", "OVERSCAN CALIBRATION"]
    }

    function settingsValues() {
        return ["PER CHANNEL >",
                tvModeBackend.episodeOrder().toUpperCase(),
                tvModeBackend.adsOnManualPick() ? "ON" : "OFF",
                sleepLabel(),
                "ADJUST >"]
    }

    function openSettings() {
        settingsOpen = true
        mpvController.clearOverlay(tvModeBackend.overlayIdGuide())
        drawSettings()
    }

    function closeSettings() {
        settingsOpen = false
        mpvController.clearOverlay(tvModeBackend.overlayIdSettings())
    }

    function drawSettings() {
        mpvController.setOverlay(tvModeBackend.overlayIdSettings(),
                                 tvModeBackend.settingsAss(guideColors(),
                                                           "TV SETTINGS",
                                                           settingsLabels(),
                                                           settingsValues(),
                                                           settingsRow, 0,
                                                           settingsRows.length,
                                                           "UP/DN: OPTION   SELECT: CHANGE   BACK: GUIDE",
                                                           false),
                                 tvModeBackend.canvasWidth(),
                                 tvModeBackend.canvasHeight())
    }

    function settingsKey(event) {
        if (event.isAutoRepeat
                && event.key !== Qt.Key_Up && event.key !== Qt.Key_Down)
            return
        var n = settingsRows.length
        if (event.key === Qt.Key_Up) {
            settingsRow = (settingsRow <= 0) ? n - 1 : settingsRow - 1
            drawSettings()
        } else if (event.key === Qt.Key_Down) {
            settingsRow = (settingsRow >= n - 1) ? 0 : settingsRow + 1
            drawSettings()
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            activateSetting()
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                   || event.key === Qt.Key_Back) {
            closeSettings()
            drawGuide()
        }
    }

    function activateSetting() {
        var id = settingsRows[settingsRow]
        if (id === "channel_order") {
            openChannelOrder()
        } else if (id === "episode_order") {
            // Applies to every channel and clears the per-channel overrides,
            // which is why it is worded "ALL CHANNELS".
            var next = tvModeBackend.episodeOrder() === "sequential" ? "random"
                                                                     : "sequential"
            tvModeBackend.setEpisodeOrder(next)
            appCore.save_setting(moduleRoot.moduleId, "episode_order", next)
            saveChannelOrders()
            drawSettings()
        } else if (id === "ads") {
            var on = !tvModeBackend.adsOnManualPick()
            tvModeBackend.setAdsOnManualPick(on)
            appCore.save_setting(moduleRoot.moduleId, "ads_on_manual_pick", on)
            drawSettings()
        } else if (id === "sleep") {
            cycleSleep()
            drawSettings()
        } else if (id === "overscan") {
            openCalibrate()
        }
    }

    // -- per-channel episode order -------------------------------------------
    // Every channel and its order in one scrolling list, so the whole lineup is
    // visible and any channel can be toggled — not just whichever one the guide
    // happened to be sitting on.
    property bool orderOpen:      false
    property int  orderRow:       0
    property int  orderFirstRow:  0
    // Not a constant: the overscan screen can change the safe area, and a
    // stale larger value would let the selected row scroll off the bottom.
    property int orderVisibleRows: 10

    function orderLabels() {
        var out  = []
        var list = tvModeBackend.channels()
        for (var i = 0; i < list.length; i++)
            out.push(pad2(list[i].number) + "  " + String(list[i].name).toUpperCase())
        return out
    }

    function orderValues() {
        var out = []
        for (var i = 0; i < tvModeBackend.channelCount(); i++)
            out.push(tvModeBackend.channelOrder(i).toUpperCase())
        return out
    }

    function openChannelOrder() {
        orderVisibleRows = tvModeBackend.optionRowCapacity()
        // Open on the channel the guide was last sitting on: usually the one
        // you came in to change.
        orderRow = Math.max(0, Math.min(guideLastChannelRow,
                                        tvModeBackend.channelCount() - 1))
        orderFirstRow = 0
        scrollOrderIntoView()
        orderOpen = true
        mpvController.clearOverlay(tvModeBackend.overlayIdSettings())
        drawChannelOrder()
    }

    function closeChannelOrder() {
        orderOpen = false
        mpvController.clearOverlay(tvModeBackend.overlayIdSettings())
    }

    function scrollOrderIntoView() {
        if (orderRow < orderFirstRow)
            orderFirstRow = orderRow
        else if (orderRow >= orderFirstRow + orderVisibleRows)
            orderFirstRow = orderRow - orderVisibleRows + 1
        if (orderFirstRow < 0) orderFirstRow = 0
    }

    function drawChannelOrder() {
        mpvController.setOverlay(tvModeBackend.overlayIdSettings(),
                                 tvModeBackend.settingsAss(guideColors(),
                                                           "CHANNEL ORDER",
                                                           orderLabels(),
                                                           orderValues(),
                                                           orderRow, orderFirstRow,
                                                           orderVisibleRows,
                                                           "UP/DN: CHANNEL   SELECT: TOGGLE   BACK: SETTINGS",
                                                           false),
                                 tvModeBackend.canvasWidth(),
                                 tvModeBackend.canvasHeight())
    }

    function channelOrderKey(event) {
        if (event.isAutoRepeat
                && event.key !== Qt.Key_Up && event.key !== Qt.Key_Down)
            return
        var n = tvModeBackend.channelCount()
        if (n <= 0) { closeChannelOrder(); openSettings(); return }
        if (event.key === Qt.Key_Up) {
            orderRow = (orderRow <= 0) ? n - 1 : orderRow - 1
            scrollOrderIntoView(); drawChannelOrder()
        } else if (event.key === Qt.Key_Down) {
            orderRow = (orderRow >= n - 1) ? 0 : orderRow + 1
            scrollOrderIntoView(); drawChannelOrder()
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            tvModeBackend.setChannelOrder(orderRow,
                tvModeBackend.channelOrder(orderRow) === "sequential" ? "random"
                                                                     : "sequential")
            saveChannelOrders()
            drawChannelOrder()
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                   || event.key === Qt.Key_Back) {
            closeChannelOrder()
            openSettings()
        }
    }

    // -- overscan adjust -----------------------------------------------------
    // The calibration pattern with a compact readout over it: pick an edge,
    // nudge it, watch the boxes move against the tube. Values apply live and
    // are written back on the way out.
    property bool calibrateOpen: false
    property int  calibrateEdge: 0
    readonly property var calibrateEdges: ["left", "right", "top", "bottom"]
    readonly property real calibrateStep: 0.005

    function openCalibrate() {
        calibrateOpen = true
        calibrateEdge = 0
        calibrating   = true
        mpvController.clearOverlay(tvModeBackend.overlayIdSettings())
        drawCalibrate()
    }

    function closeCalibrate() {
        calibrateOpen = false
        calibrating   = false
        mpvController.clearOverlay(tvModeBackend.overlayIdCalibrate())
        mpvController.clearOverlay(tvModeBackend.overlayIdSettings())
    }

    function drawCalibrate() {
        // Pattern first, readout second: separate overlay ids, so nudging a
        // value redraws both without either flickering the other.
        mpvController.setOverlay(tvModeBackend.overlayIdCalibrate(),
                                 tvModeBackend.calibrationAss(),
                                 tvModeBackend.canvasWidth(),
                                 tvModeBackend.canvasHeight())
        var values = []
        for (var i = 0; i < calibrateEdges.length; i++)
            values.push((tvModeBackend.safeEdge(calibrateEdges[i]) * 100).toFixed(1) + "%")
        mpvController.setOverlay(tvModeBackend.overlayIdSettings(),
                                 tvModeBackend.settingsAss(guideColors(), "OVERSCAN",
                                                           ["LEFT", "RIGHT", "TOP", "BOTTOM"],
                                                           values, calibrateEdge,
                                                           0, calibrateEdges.length,
                                                           "UP/DN: EDGE   L/R: ADJUST   BACK: SAVE",
                                                           true),
                                 tvModeBackend.canvasWidth(),
                                 tvModeBackend.canvasHeight())
    }

    function nudgeEdge(delta) {
        var edge = calibrateEdges[calibrateEdge]
        tvModeBackend.setSafeEdge(edge, tvModeBackend.safeEdge(edge) + delta)
        drawCalibrate()
    }

    function saveCalibration() {
        for (var i = 0; i < calibrateEdges.length; i++) {
            var e = calibrateEdges[i]
            appCore.save_setting(moduleRoot.moduleId, "safe_" + e,
                                 tvModeBackend.safeEdge(e))
        }
    }

    function calibrateKey(event) {
        // Every key here is an adjustment except the exits, so repeat is what
        // you want: hold LEFT to walk an edge in rather than tapping 20 times.
        if (event.key === Qt.Key_Up) {
            calibrateEdge = (calibrateEdge <= 0) ? calibrateEdges.length - 1
                                                 : calibrateEdge - 1
            drawCalibrate()
        } else if (event.key === Qt.Key_Down) {
            calibrateEdge = (calibrateEdge >= calibrateEdges.length - 1) ? 0
                                                                         : calibrateEdge + 1
            drawCalibrate()
        } else if (event.key === Qt.Key_Left) {
            nudgeEdge(-calibrateStep)
        } else if (event.key === Qt.Key_Right) {
            nudgeEdge(calibrateStep)
        } else if (!event.isAutoRepeat
                   && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                       || event.key === Qt.Key_Back || event.key === Qt.Key_Return
                       || event.key === Qt.Key_Enter)) {
            saveCalibration()
            closeCalibrate()
            // The safe area just changed; anything sized against it is stale.
            orderVisibleRows    = tvModeBackend.optionRowCapacity()
            episodesVisibleRows = tvModeBackend.episodeRowCapacity()
            openSettings()
        }
    }

    // -- episode list --------------------------------------------------------
    // Every episode on one channel, opened from the guide's channel-name cell.
    // Drawn as its own overlay on top of the guide, which stays up underneath so
    // BACK returns there rather than to the programme.
    property bool episodesOpen:      false
    property int  episodesChannel:   0
    property int  episodesRow:       0
    property int  episodesFirstRow:  0
    // Cached on open: the backend rebuilds the whole title list per call, and a
    // 482-episode channel would otherwise re-derive it on every key press.
    property int  episodesCount:     0
    property int episodesVisibleRows: 10

    function openEpisodeList(channelIndex) {
        episodesCount = tvModeBackend.channelEpisodeTitles(channelIndex).length
        if (episodesCount <= 0) {
            showMessage("NO EPISODES")
            return
        }
        episodesVisibleRows = tvModeBackend.episodeRowCapacity()
        episodesChannel  = channelIndex
        episodesRow      = 0
        episodesFirstRow = 0
        episodesOpen     = true
        mpvController.clearOverlay(tvModeBackend.overlayIdGuide())
        drawEpisodeList()
    }

    function closeEpisodeList() {
        episodesOpen = false
        mpvController.clearOverlay(tvModeBackend.overlayIdEpisodes())
    }

    function drawEpisodeList() {
        mpvController.setOverlay(tvModeBackend.overlayIdEpisodes(),
                                 tvModeBackend.episodeListAss(guideColors(),
                                                              episodesChannel,
                                                              episodesRow,
                                                              episodesFirstRow,
                                                              episodesVisibleRows),
                                 tvModeBackend.canvasWidth(),
                                 tvModeBackend.canvasHeight())
    }

    function scrollEpisodesIntoView() {
        if (episodesRow < episodesFirstRow)
            episodesFirstRow = episodesRow
        else if (episodesRow >= episodesFirstRow + episodesVisibleRows)
            episodesFirstRow = episodesRow - episodesVisibleRows + 1
        if (episodesFirstRow < 0) episodesFirstRow = 0
    }

    function episodesKey(event) {
        // Up/Down repeat — a 482-episode channel is unusable without it. Select
        // and back do not.
        if (event.isAutoRepeat
                && event.key !== Qt.Key_Up && event.key !== Qt.Key_Down)
            return
        if (event.key === Qt.Key_Up) {
            episodesRow = (episodesRow <= 0) ? episodesCount - 1 : episodesRow - 1
            scrollEpisodesIntoView(); drawEpisodeList()
        } else if (event.key === Qt.Key_Down) {
            episodesRow = (episodesRow >= episodesCount - 1) ? 0 : episodesRow + 1
            scrollEpisodesIntoView(); drawEpisodeList()
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // Same as a guide-grid pick: remember first, while the lineup still
            // points at the channel we are leaving.
            rememberPosition()
            // playChannelEpisode tunes the channel itself, and puts a break in
            // front when the setting is on — either way it hands back something
            // playable, so this path stays the same.
            var req = tvModeBackend.playChannelEpisode(episodesChannel, episodesRow)
            closeEpisodeList()
            closeGuide()
            play(req, true)
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                   || event.key === Qt.Key_Back) {
            closeEpisodeList()
            drawGuide()
        }
    }

    // -- overscan calibration ------------------------------------------------
    // Reached only via the guide's SETTINGS row. It used to be a bare ENTER
    // during playback, which meant picking a channel — ENTER in the guide, which
    // then closes — could drop a second ENTER onto the toggle and flash the
    // setup grid onto the tube. Navigating to it removes that whole class of
    // accident, so no debounce is needed.
    // Kept only so clearOverlays() can report the pattern is gone; the pattern
    // itself is drawn and torn down by openCalibrate()/closeCalibrate().
    property bool calibrating: false

    Timer {
        id: bannerTimer
        interval: tvModeBackend ? tvModeBackend.bannerDurationMs() : 4000
        repeat: false
        onTriggered: mpvController.clearOverlay(tvModeBackend.overlayIdChannel())
    }

    Timer {
        id: messageTimer
        interval: 6000
        repeat: false
        onTriggered: mpvController.clearOverlay(tvModeBackend.overlayIdMessage())
    }

    // Hand a backend decision to mpv.
    //
    // `isChange` distinguishes flipping channels from the two cases that should
    // just cut: the first tune-in, and rolling the next episode when one ends.
    // Only a genuine channel change gets a transition.
    function play(req, isChange) {
        var hadPlayback = currentPath !== ""

        if (!req || !req.valid) {
            currentPath = ""
            switchTimer.stop()
            pendingBanner = null
            // An empty channel shows colour bars rather than a black screen —
            // a dead channel on a real set still had *something* on it.
            var bars = tvModeBackend.colorbarsClip()
            if (bars !== "")
                mpvController.sessionLoop(bars)
            else
                mpvController.sessionStop()
            var n = (req && req.number !== undefined) ? req.number : 0
            showMessage("CH " + pad2(n) + "   NO SIGNAL")
            console.log("[tv_mode] channel has no episodes")
            return
        }

        // A commercial is not the show: it gets no banner, and its position is
        // never remembered for resume.
        var isAd = req.commercial === true
        currentPath = isAd ? "" : req.path
        lastPositionMs = 0

        var clip = isChange ? tvModeBackend.transitionClip() : ""
        if (isAd) {
            switchTimer.stop()
            pendingBanner = null
            mpvController.sessionLoad(req.path, 0)
            console.log("[tv_mode] AD ->", req.path)
            return
        }

        if (clip !== "") {
            // Burst of snow, then the episode. --prefetch-playlist has the
            // episode decoded before the burst ends, so the cut is instant.
            switchTimer.stop()
            pendingBanner = null
            mpvController.sessionTransition(clip, req.path, req.start,
                                            tvModeBackend.transitionSeconds())
            showBanner(req.number, req.name)
        } else if (isChange && hadPlayback && tvModeBackend.bridgeMs() > 0) {
            // No transition clip: keep the outgoing show on screen while the next
            // one loads, then cut over. Avoids the frozen frame a bare load would
            // leave. The banner waits for the cut — see switchTimer.
            mpvController.sessionPreload(req.path, req.start)
            pendingBanner = { "number": req.number, "name": req.name }
            switchTimer.interval = tvModeBackend.bridgeMs()
            switchTimer.restart()
        } else {
            switchTimer.stop()
            pendingBanner = null
            mpvController.sessionLoad(req.path, req.start)
            showBanner(req.number, req.name)
        }
        // Remember the channel so auto-tune resumes here next time.
        if (req.number !== undefined)
            appCore.save_setting(moduleRoot.moduleId, "last_channel", req.number)
        console.log("[tv_mode] CH", req.number, req.name, "->", req.path, "@", req.start)
    }

    // Cuts over to the preloaded channel once the bridge window elapses. The
    // banner is flashed here, at the moment the picture actually changes, rather
    // than when the button was pressed.
    Timer {
        id: switchTimer
        repeat: false
        onTriggered: {
            mpvController.sessionCommitSwitch()
            if (pendingBanner !== null) {
                sessionRoot.showBanner(pendingBanner.number, pendingBanner.name)
                pendingBanner = null
            }
        }
    }

    // -- on-screen display --------------------------------------------------
    // The banner is drawn by mpv as an ASS overlay, not by Qt: mpv owns the
    // framebuffer during a session, so anything Qt painted would be invisible.
    function showBanner(number, name) {
        lastNumber = number
        lastName   = name
        mpvController.setOverlay(tvModeBackend.overlayIdChannel(),
                                 tvModeBackend.channelBannerAss(number, name),
                                 tvModeBackend.canvasWidth(),
                                 tvModeBackend.canvasHeight())
        bannerTimer.restart()
    }

    function showMessage(text, position) {
        mpvController.setOverlay(tvModeBackend.overlayIdMessage(),
                                 tvModeBackend.messageAss(text, position || "top"),
                                 tvModeBackend.canvasWidth(),
                                 tvModeBackend.canvasHeight())
        messageTimer.restart()
    }

    function clearOverlays() {
        switchTimer.stop()
        pendingBanner = null
        bannerTimer.stop()
        messageTimer.stop()
        mpvController.clearOverlay(tvModeBackend.overlayIdChannel())
        mpvController.clearOverlay(tvModeBackend.overlayIdMessage())
        mpvController.clearOverlay(tvModeBackend.overlayIdCalibrate())
        mpvController.clearOverlay(tvModeBackend.overlayIdGuide())
        mpvController.clearOverlay(tvModeBackend.overlayIdEpisodes())
        mpvController.clearOverlay(tvModeBackend.overlayIdSettings())
        calibrating = false
        guideOpen = false
        episodesOpen = false
        settingsOpen = false
        calibrateOpen = false
        orderOpen = false
        transportOpen = false
    }

    function pad2(n) { return ("0" + n).slice(-2) }

    // -- sleep timer --------------------------------------------------------
    // Each press advances the same ladder the TV uses (15/30/45/60/90/off), so a
    // single SLEEP press sets both. The OSD always reports what the *Pi* will do:
    // the two counters can drift (Sony's first press re-displays rather than
    // increments), and the Pi's timer is the one that matters for card safety.
    property int sleepStep: -1

    function sleepLabel() {
        var ladder = tvModeBackend.sleepLadder()
        if (!ladder || ladder.length === 0 || sleepStep < 0) return "OFF"
        var mins = ladder[sleepStep]
        return mins > 0 ? mins + " MIN" : "OFF"
    }

    function cycleSleep() {
        var ladder = tvModeBackend.sleepLadder()
        if (!ladder || ladder.length === 0) return
        sleepStep = (sleepStep + 1) % ladder.length
        var mins = ladder[sleepStep]
        if (mins <= 0) {
            sleepTimer.stop()
            showMessage("SLEEP  OFF", "left")
        } else {
            sleepTimer.interval = mins * 60000
            sleepTimer.restart()
            showMessage("SLEEP  " + mins + " MIN", "left")
        }
    }

    Timer {
        id: sleepTimer
        repeat: false
        onTriggered: {
            if (tvModeBackend.sleepAction() === "poweroff") {
                // Quitting is the shutdown: under the autostart service the unit's
                // ExecStopPost (240mp-stop) powers the Pi off for any exit status
                // that isn't 10 or 11. No sudo, no extra privileges.
                sessionRoot.clearOverlays()
                mpvController.endSession()
                Qt.quit()
            } else {
                // Leave the Pi running and reachable — it cannot be woken from a
                // halt by the remote, only by GPIO3 or a power cycle.
                mpvController.sessionStop()
                sessionRoot.currentPath = ""
                sessionRoot.showMessage("SLEEP", "left")
            }
        }
    }

    function rememberPosition() {
        if (currentPath !== "" && lastPositionMs > 0)
            tvModeBackend.rememberPosition(currentPath, lastPositionMs / 1000.0)
    }

    function leaveSession() {
        if (sessionEnded) return
        sessionEnded = true
        rememberPosition()
        clearOverlays()
        // Ends mpv; playbackEnded fires after the DRM/VT restore and pops us.
        mpvController.endSession()
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) sessionRoot.lastPositionMs = ms
        }

        // The current episode reached its natural end while mpv stayed alive.
        // Roll the next one on this channel — the box never stops broadcasting.
        function onFileEnded() {
            if (sessionRoot.sessionEnded) return
            sessionRoot.play(tvModeBackend.advanceCurrent(), false)
        }

        // The mpv process exited. In a session this happens once, at the end.
        // If it fires without us asking, mpv died — leave rather than sit on a
        // dead subprocess, which is the failure the other Player views guard.
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            if (!sessionRoot.sessionEnded)
                console.warn("[tv_mode] session ended unexpectedly:", reason)
            sessionRoot.sessionEnded = true
            sessionRoot.goBack()
        }
    }

    Component.onCompleted: {
        mpvController.startSession(tvModeBackend.initialVolume())
        tvModeBackend.selectIndex(startIndex)
        // The session needs a moment to come up and connect its IPC socket
        // before it will accept a loadfile.
        startTimer.start()
    }

    Timer {
        id: startTimer
        interval: 700
        repeat: false
        onTriggered: {
            sessionRoot.play(tvModeBackend.tuneInCurrent(), false)
            if (sessionRoot.openGuideOnStart)
                sessionRoot.openGuide()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }
}
