import QtQuick

FocusScope {
    id: guideRoot

    property var navParams: ({})
    property string moduleId: navParams.moduleId || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    readonly property int columnCount: 3
    readonly property real halfHourMs: 30 * 60 * 1000
    readonly property real spanMs: columnCount * halfHourMs

    property real fromMs: 0
    property var rows: []
    property int windowOffset: 0
    property real nowMs: 0

    property bool autoScroll: true
    property bool autoScrollEnabled: true
    property int resumeSeconds: 30
    property bool musicStartedHere: false
    property string status: ""

    property string previewMode: "VIDEO"

    property int masterVolume: 100

    readonly property bool previewAudible:
        previewShowsVideo && previewLoader.item !== null && previewLoader.item.active
        && previewLoader.item.canPlayAudio

    onPreviewAudibleChanged: applyDuck()

    function applyDuck() {
        if (typeof weatherBackend === "undefined") return
        weatherBackend.duck_music(previewAudible, 400)
    }

    function applyVolume() {
        if (previewLoader.item) previewLoader.item.volume = masterVolume
        if (typeof weatherBackend !== "undefined")
            weatherBackend.set_music_volume(masterVolume)
    }

    function reloadVolume() {
        var mv = appCore.get_setting(guideRoot.moduleId, "volume")
        if (mv === undefined || mv === null || String(mv) === "") return
        var v = Math.max(0, Math.min(100, parseInt(mv)))
        if (v === masterVolume) return
        masterVolume = v
        applyVolume()
    }
    readonly property bool previewShowsPicture: previewMode !== "OFF"
    readonly property bool previewShowsVideo:   previewMode === "VIDEO"

    property var nowPlaying: ({})
    property int  detailFor: -1

    readonly property int previewSettleMs: 700

    readonly property int previewDwellMs: 2500

    Timer {
        id: previewSettle
        interval: guideRoot.previewSettleMs
        repeat: false
        onTriggered: guideRoot.startPreview()
    }

    Timer {
        id: previewDwell
        interval: guideRoot.previewDwellMs
        repeat: false
        onTriggered: {
            if (!guideRoot.showDetail) return
            var i = channelRows.currentIndex
            if (i < 0 || i >= guideRoot.rows.length) return
            var row = guideRoot.rows[i]
            if (row.isSpecial) return
            guideRoot.dwellChannel = row.number
            virtualChannelsBackend.preview_stream(row.number)
        }
    }

    property int dwellChannel: -1

    property string previewImage: ""
    property int previewSlot: -1
    property bool previewStarted: false

    Timer {
        id: previewFollow
        interval: 1000
        repeat: true
        running: guideRoot.showDetail && guideRoot.previewShowsPicture
                 && guideRoot.previewStarted
        onTriggered: guideRoot.followSchedule()
    }

    function followSchedule() {
        if (!showDetail || !previewShowsPicture) return
        var i = channelRows.currentIndex
        if (i < 0 || i >= rows.length) return
        var row = rows[i]
        if (row.isSpecial) return

        var slot = virtualChannelsBackend.current_slot(row.number)
        if (slot === previewSlot) return

        if (slot < 0) {
            previewSlot = -1
            previewImage = ""
            if (previewLoader.item) previewLoader.item.clear()
            return
        }
        showCurrent(row.number, true)
        refreshNowPlaying()
    }

    function startPreview() {
        if (!showDetail) { stopPreview(); return }
        var i = channelRows.currentIndex
        if (i < 0 || i >= rows.length) { stopPreview(); return }
        var row = rows[i]
        if (row.isSpecial) { stopPreview(); return }

        if (!previewShowsPicture) return
        previewStarted = true
        showCurrent(row.number, false)
    }

    function showCurrent(channelNumber, following) {
        var src = virtualChannelsBackend.preview_source(channelNumber,
                                                        Math.round(screen.width),
                                                        Math.round(screen.height))
        if (!src) { stopPreview(); return }

        previewImage = src.image || ""
        previewSlot  = src.slotIndex !== undefined ? src.slotIndex : -1
        if (!previewShowsVideo) return

        if (src.valid === true && previewLoader.item) {
            previewLoader.item.show(src.url, src.positionMs)
        } else if (previewLoader.item) {
            previewLoader.item.clear()
            if (following) {
                dwellChannel = channelNumber
                virtualChannelsBackend.preview_stream(channelNumber)
            } else {
                previewDwell.restart()
            }
        }
    }

    function stopPreview() {
        previewSettle.stop()
        previewDwell.stop()
        dwellChannel = -1
        previewSlot = -1
        previewStarted = false
        virtualChannelsBackend.cancel_preview_stream()
        previewImage = ""
        if (previewLoader.item) previewLoader.item.clear()
    }

    function refreshNowPlaying() {
        if (autoScroll) { nowPlaying = ({}); detailFor = -1; return }
        var i = channelRows.currentIndex
        if (i < 0 || i >= rows.length) { nowPlaying = ({}); detailFor = -1; return }
        var row = rows[i]
        detailFor = row.number
        nowPlaying = row.isSpecial ? ({ valid: false, message: row.name })
                                   : virtualChannelsBackend.now_next(row.number)
    }

    function refreshDetail() {
        stopPreview()
        if (showDetail) previewSettle.restart()
        if (autoScroll) { nowPlaying = ({}); detailFor = -1; return }
        var i = channelRows.currentIndex
        if (i < 0 || i >= rows.length) { nowPlaying = ({}); detailFor = -1; return }
        var row = rows[i]
        detailFor = row.number
        nowPlaying = row.isSpecial ? ({ valid: false, message: row.name })
                                   : virtualChannelsBackend.now_next(row.number)
    }

    function runLabel(ms) {
        var total = Math.round(Number(ms) / 1000)
        if (!isFinite(total) || total <= 0) return ""
        var m = Math.floor(total / 60)
        var sec = total % 60
        if (m >= 60) {
            var h = Math.floor(m / 60)
            return h + "H " + (m % 60) + "M"
        }
        return m > 0 ? m + "M " + (sec > 0 ? sec + "S" : "") : sec + "S"
    }

    readonly property real edgeMargin: root.sw * 0.045
    readonly property real topMargin:  root.sh * 0.055

    focus: true

    readonly property string weatherView: "WeatherChannel.qml"

    function refresh() {
        nowMs = virtualChannelsBackend.now_ms()
        fromMs = Math.floor(nowMs / halfHourMs) * halfHourMs + windowOffset * halfHourMs
        var real = virtualChannelsBackend.guide_grid(fromMs, spanMs)

        if (toggleOn("channels.weather", false)) {
            var wx = { number: virtualChannelsBackend.weather_channel_number(),
                       name: "Weather", isSpecial: true,
                       viewPath: weatherView,
                       blocks: [{ startMs: fromMs, durMs: spanMs,
                                  title: "Local Forecast", series: "",
                                  ep: "", onNow: true }] }
            var withWx = []
            for (var i = 0; i < real.length; i++) withWx.push(real[i])
            withWx.push(wx)
            withWx.sort(function(a, b) { return a.number - b.number })
            rows = withWx
        } else {
            rows = real
        }
    }

    function openSpecial(row) {
        navigateTo(row.viewPath, {}, { fromGuide: true })
    }

    function timeLabel(ms) { return Qt.formatDateTime(new Date(ms), "h:mm AP") }

    function toggleOn(key, dflt) {
        var v = appCore.get_setting(guideRoot.moduleId, key)
        if (v === undefined || v === null || v === "") return dflt
        if (typeof v === "string") return v.toUpperCase() === "ON"
        return !!v
    }

    Connections {
        target: virtualChannelsBackend
        function onPreviewStreamReady(ch, url, positionMs) {
            if (!guideRoot.showDetail || ch !== guideRoot.dwellChannel) return
            if (previewLoader.item) previewLoader.item.show(url, positionMs)
        }
    }

    Component.onCompleted: {
        autoScrollEnabled = toggleOn("guide.autoscroll", true)
        autoScroll = autoScrollEnabled
        var rs = appCore.get_setting(guideRoot.moduleId, "guide.resume_seconds")
        if (rs !== undefined && rs !== null && rs !== "") resumeSeconds = parseInt(rs) || 0
        var pv = appCore.get_setting(guideRoot.moduleId, "guide.preview")
        if (pv !== undefined && pv !== null && String(pv) !== "")
            previewMode = String(pv).toUpperCase()
        var mv = appCore.get_setting(guideRoot.moduleId, "volume")
        if (mv !== undefined && mv !== null && String(mv) !== "")
            masterVolume = Math.max(0, Math.min(100, parseInt(mv)))
        applyVolume()
        refresh()

        if (idleTracker) idleTracker.mpvActive = true

        if (toggleOn("guide.music", false) && typeof weatherBackend !== "undefined") {
            var wasPlaying = weatherBackend.musicPlaying
            weatherBackend.startMusic()
            musicStartedHere = !wasPlaying && weatherBackend.musicPlaying
            if (!weatherBackend.musicPlaying)
                status = "Music needs the Weather module's Music setting on"
        }
    }

    Component.onDestruction: {
        stopPreview()
        if (typeof weatherBackend !== "undefined") weatherBackend.duck_music(false, 250)
        if (idleTracker) {
            idleTracker.mpvActive = false
            idleTracker.resetActivity()
        }
        if (musicStartedHere && typeof weatherBackend !== "undefined")
            weatherBackend.stopMusic()
    }

    Timer { interval: 30000; running: true; repeat: true; onTriggered: guideRoot.refresh() }
    Timer { interval: 5000; running: true; repeat: true; onTriggered: guideRoot.reloadVolume() }
    Timer { interval: 10000; running: true; repeat: true
            onTriggered: {
                guideRoot.nowMs = virtualChannelsBackend.now_ms()
                if (guideRoot.showDetail) guideRoot.refreshNowPlaying()
            } }

    Timer {
        id: resumeTimer
        interval: Math.max(1, guideRoot.resumeSeconds) * 1000
        running: guideRoot.autoScrollEnabled && !guideRoot.autoScroll
                 && guideRoot.resumeSeconds > 0
        repeat: false
        onTriggered: {
            guideRoot.windowOffset = 0
            guideRoot.refresh()
            guideRoot.autoScroll = true
            guideRoot.stopPreview()
            guideRoot.refreshDetail()
        }
    }

    function takeControl() {
        var wasCrawling = autoScroll
        autoScroll = false
        resumeTimer.restart()
        if (wasCrawling) refreshDetail()
    }

    function tuneFromGuide(direction) {
        stopPreview()
        if (rows.length === 0) return
        var i = direction > 0 ? 0 : rows.length - 1
        var row = rows[i]
        if (row.isSpecial) { openSpecial(row); return }
        navigateTo("Player.qml", {
            channelNumber: row.number,
            channelName:   row.name
        }, { fromGuide: true })
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_ChannelUp || event.key === Qt.Key_PageUp) {
            tuneFromGuide(1); event.accepted = true; return
        }
        if (event.key === Qt.Key_ChannelDown || event.key === Qt.Key_PageDown) {
            tuneFromGuide(-1); event.accepted = true; return
        }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            guideRoot.takeControl()
            var n = guideRoot.rows.length
            if (n > 0)
                channelRows.currentIndex = (channelRows.currentIndex - 1 + n) % n
            guideRoot.refreshDetail()
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            guideRoot.takeControl()
            var count = guideRoot.rows.length
            if (count > 0)
                channelRows.currentIndex = (channelRows.currentIndex + 1) % count
            guideRoot.refreshDetail()
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            guideRoot.takeControl()
            guideRoot.windowOffset = Math.max(0, guideRoot.windowOffset - 1)
            guideRoot.refresh()
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            guideRoot.takeControl()
            guideRoot.windowOffset += 1
            guideRoot.refresh()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            var i = channelRows.currentIndex
            if (!guideRoot.autoScroll && i >= 0 && i < guideRoot.rows.length) {
                var row = guideRoot.rows[i]
                guideRoot.stopPreview()
                if (row.isSpecial) {
                    guideRoot.openSpecial(row)
                } else {
                    navigateTo("Player.qml", {
                        channelNumber: row.number,
                        channelName:   row.name
                    }, { fromGuide: true })
                }
            }
            event.accepted = true
        }
    }

    readonly property real gridWidth:   width - edgeMargin * 2
    readonly property real channelColW: gridWidth * 0.30
    readonly property real timeAreaW:   gridWidth - channelColW
    readonly property real pxPerMs:     timeAreaW / spanMs
    readonly property real rowH:        root.sh * 0.10
    readonly property real headerH:     root.sh * 0.075

    readonly property bool showDetail:  !autoScroll && rows.length > 0
    onShowDetailChanged: if (!showDetail) stopPreview()
    readonly property real detailH:     showDetail ? height * 0.42 : 0

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
    }

    Item {
        id: detail
        visible: guideRoot.showDetail
        x: guideRoot.edgeMargin
        y: guideRoot.topMargin
        width: guideRoot.gridWidth
        height: Math.max(0, guideRoot.detailH - root.sh * 0.02)

        Item {
            id: screen
            visible: guideRoot.previewShowsPicture
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            width: visible ? Math.min(parent.width * 0.5, height * 4 / 3) : 0
            clip: true

            Rectangle { anchors.fill: parent; color: "black" }

            Image {
                id: previewStill
                anchors.fill: parent
                source: guideRoot.previewImage
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                visible: status === Image.Ready
            }

            Loader {
                id: previewLoader
                anchors.fill: parent
                asynchronous: true
                onLoaded: {
                    item.finished.connect(guideRoot.followSchedule)
                    item.volume = guideRoot.masterVolume
                }
                active: guideRoot.previewShowsVideo
                source: "ChannelPreview.qml"
                onStatusChanged: {
                    if (status === Loader.Error)
                        console.log("[guide] no video preview available; showing the still instead")
                }
            }
        }

        Item {
            id: about
            anchors.left: screen.right
            anchors.leftMargin: guideRoot.previewShowsPicture ? root.sw * 0.025 : 0
            anchors.right: parent.right
            height: parent.height

            Text {
                id: seriesLine
                text: {
                    var n = guideRoot.nowPlaying
                    if (!n || n.valid !== true) return n && n.message ? n.message : "Off air"
                    return n.series ? n.series : (n.title || "")
                }
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.top: parent.top
                anchors.left: parent.left
                width: parent.width
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.040
            }

            Text {
                id: epLine
                text: {
                    var n = guideRoot.nowPlaying
                    if (!n || n.valid !== true) return ""
                    if (!n.series) return n.ep ? n.ep : ""
                    var bits = []
                    if (n.ep) bits.push(n.ep)
                    if (n.title) bits.push(n.title)
                    return bits.join("  ")
                }
                visible: text !== ""
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.top: seriesLine.bottom
                anchors.topMargin: root.sh * 0.006
                anchors.left: parent.left
                width: parent.width
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.030
            }

            Text {
                id: runLine
                text: {
                    var n = guideRoot.nowPlaying
                    if (!n || n.valid !== true) return ""
                    var bits = [guideRoot.runLabel(n.durMs)]
                    if (n.nextTitle) bits.push("NEXT: " + n.nextTitle)
                    return bits.join("   ·   ")
                }
                visible: text !== ""
                color: root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.top: epLine.visible ? epLine.bottom : seriesLine.bottom
                anchors.topMargin: root.sh * 0.010
                anchors.left: parent.left
                width: parent.width
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.024
            }

            Item {
                id: progress
                visible: guideRoot.nowPlaying && guideRoot.nowPlaying.valid === true
                anchors.top: runLine.visible ? runLine.bottom : epLine.bottom
                anchors.topMargin: root.sh * 0.012
                anchors.left: parent.left
                width: parent.width
                height: root.sh * 0.050

                Text {
                    id: elapsed
                    text: {
                        var n = guideRoot.nowPlaying
                        if (!n || n.valid !== true) return ""
                        return guideRoot.runLabel(n.offsetMs) + " IN"
                    }
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.top: parent.top
                    anchors.left: parent.left
                    font.pixelSize: root.sh * 0.022
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    width: parent.width
                    height: root.sh * 0.010
                    property color baseColor: root.primaryColor
                    color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.2)

                    Rectangle {
                        height: parent.height
                        color: root.accentColor
                        width: {
                            var n = guideRoot.nowPlaying
                            if (!n || n.valid !== true) return 0
                            var d = Number(n.durMs)
                            if (!isFinite(d) || d <= 0) return 0
                            return parent.width * Math.max(0, Math.min(1, Number(n.offsetMs) / d))
                        }
                    }
                }
            }

            Text {
                text: {
                    var n = guideRoot.nowPlaying
                    return (n && n.valid === true && n.desc) ? n.desc : ""
                }
                color: root.secondaryColor
                font.family: root.globalFont
                anchors.top: progress.visible ? progress.bottom : runLine.bottom
                anchors.topMargin: root.sh * 0.012
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: parent.width
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.024
            }
        }
    }

    Item {
        id: header
        x: guideRoot.edgeMargin
        y: guideRoot.topMargin + guideRoot.detailH
        width: guideRoot.gridWidth
        height: guideRoot.headerH

        Rectangle { anchors.fill: parent; color: root.accentColor }

        Text {
            text: Qt.formatDateTime(new Date(guideRoot.fromMs), "ddd d MMM")
            color: root.surfaceColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: root.sw * 0.0078125
            width: guideRoot.channelColW
            elide: Text.ElideRight
            font.pixelSize: root.sh * 0.030
        }

        Repeater {
            model: guideRoot.columnCount
            delegate: Text {
                required property int index
                text: guideRoot.timeLabel(guideRoot.fromMs + index * guideRoot.halfHourMs)
                color: root.surfaceColor
                font.family: root.globalFont
                anchors.verticalCenter: parent.verticalCenter
                x: guideRoot.channelColW + index * (guideRoot.timeAreaW / guideRoot.columnCount)
                   + root.sw * 0.0046875
                font.pixelSize: root.sh * 0.030
            }
        }
    }

    ListView {
        id: channelRows
        x: guideRoot.edgeMargin
        y: header.y + header.height
        width: guideRoot.gridWidth
        height: guideRoot.height - y
                - (guideRoot.autoScroll ? guideRoot.topMargin * 0.5 : root.sh * 0.06)
        clip: true
        interactive: false
        currentIndex: 0
        highlightMoveDuration: 0
        highlightFollowsCurrentItem: !guideRoot.autoScroll

        readonly property int setHeight: Math.max(1, guideRoot.rows.length * guideRoot.rowH)
        readonly property int copies: guideRoot.rows.length > 0
                                      ? Math.max(2, Math.ceil(height / setHeight) + 1)
                                      : 0
        model: guideRoot.autoScroll ? guideRoot.rows.length * copies
                                    : guideRoot.rows.length

        delegate: Item {
            width: channelRows.width
            height: guideRoot.rowH

            required property int index
            readonly property var rowData: guideRoot.rows.length > 0
                                           ? guideRoot.rows[index % guideRoot.rows.length]
                                           : null
            readonly property bool selected: !guideRoot.autoScroll
                                             && index === channelRows.currentIndex

            Rectangle {
                anchors.fill: parent
                color: parent.selected ? root.accentColor : "transparent"
                border.color: root.tertiaryColor
                border.width: 1
            }

            Rectangle {
                visible: !parent.selected
                x: 1
                y: 1
                width: guideRoot.channelColW - 1
                height: parent.height - 2
                property color baseColor: root.primaryColor
                color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.13)
            }

            Text {
                id: chNum
                text: parent.rowData ? parent.rowData.number : ""
                color: parent.selected ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0078125
                width: root.sw * 0.05
                horizontalAlignment: Text.AlignRight
                font.pixelSize: root.sh * 0.036
            }
            Text {
                text: parent.rowData ? parent.rowData.name : ""
                color: parent.selected ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: chNum.right
                anchors.leftMargin: root.sw * 0.009375
                width: guideRoot.channelColW - chNum.width - root.sw * 0.022
                elide: Text.ElideRight
                font.pixelSize: root.sh * 0.033
            }

            Repeater {
                model: parent.rowData ? parent.rowData.blocks : []
                delegate: Item {
                    required property var modelData

                    readonly property real rawX: (modelData.startMs - guideRoot.fromMs) * guideRoot.pxPerMs
                    readonly property real clippedX: Math.max(0, rawX)
                    readonly property real rawRight: (modelData.startMs + modelData.durMs - guideRoot.fromMs) * guideRoot.pxPerMs

                    x: guideRoot.channelColW + clippedX
                    y: 1
                    width: Math.max(0, Math.min(guideRoot.timeAreaW, rawRight) - clippedX) - 1
                    height: parent.height - 2
                    visible: width > 2

                    Rectangle {
                        anchors.fill: parent
                        color: modelData.onNow ? root.secondaryColor : root.surfaceColor
                        border.color: root.tertiaryColor
                        border.width: 1
                    }

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: root.sw * 0.0046875
                        anchors.rightMargin: root.sw * 0.003125
                        verticalAlignment: Text.AlignVCenter
                        text: {
                            var base = modelData.series
                                       ? modelData.series + ": " + modelData.title
                                       : modelData.title
                            return modelData.count > 1 ? base + "  +" + (modelData.count - 1)
                                                       : base
                        }
                        color: modelData.onNow ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        elide: Text.ElideRight
                        font.pixelSize: root.sh * 0.030
                    }
                }
            }

            Text {
                visible: !parent.rowData || !parent.rowData.blocks
                         || parent.rowData.blocks.length === 0
                x: guideRoot.channelColW + root.sw * 0.0046875
                anchors.verticalCenter: parent.verticalCenter
                text: "No listings"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.030
            }
        }

        NumberAnimation on contentY {
            running: guideRoot.autoScroll && guideRoot.rows.length > 0
            loops: Animation.Infinite
            from: 0
            to: guideRoot.rows.length * guideRoot.rowH
            duration: Math.max(1, guideRoot.rows.length * guideRoot.rowH) * 45
        }
    }

    Rectangle {
        width: 2
        height: guideRoot.autoScroll
                ? channelRows.height
                : Math.min(channelRows.height, guideRoot.rows.length * guideRoot.rowH)
        color: root.primaryColor
        opacity: 0.6
        y: channelRows.y
        x: guideRoot.edgeMargin + guideRoot.channelColW
           + (guideRoot.nowMs - guideRoot.fromMs) * guideRoot.pxPerMs
        visible: guideRoot.rows.length > 0
                 && guideRoot.nowMs >= guideRoot.fromMs
                 && guideRoot.nowMs < guideRoot.fromMs + guideRoot.spanMs
    }

    Text {
        visible: guideRoot.rows.length === 0
        anchors.centerIn: parent
        text: "No channels"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0333333
    }

    Text {
        visible: guideRoot.status !== ""
        text: guideRoot.status
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.012
        anchors.leftMargin: guideRoot.edgeMargin
        font.pixelSize: root.sh * 0.026
    }

    Text {
        visible: !guideRoot.autoScroll
        text: root.hints.back + ":BACK " + root.hints.navigate + ":CHANNEL "
              + root.hints.change + ":TIME " + root.hints.select + ":WATCH"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.bottomMargin: root.sh * 0.012
        anchors.rightMargin: guideRoot.edgeMargin
        font.pixelSize: root.sh * 0.026
    }
}
