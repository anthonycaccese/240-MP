import QtQuick
import Components

FocusScope {
    id: view

    property var navParams: ({})
    property var item: navParams.item || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    // Header
    AppBar {
        id: header
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: navParams.folder || "Details"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    // Content area
    Column {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        spacing: root.sh * 0.0416667

        // Title
        Text {
            text: item.title || "Untitled"
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0583333
            font.bold: true
            width: parent.width
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        // Only rows the server actually supplied — DLNA metadata is patchy,
        // and a column of "Unknown" tells the viewer nothing.
        // Its own Column so the rows sit tight together, rather than inheriting
        // the outer spacing that separates title from buttons.
        Column {
            spacing: 0

            Repeater {
            model: view.metaRows

            // The value is given whatever width the label leaves and wraps into
            // further lines, so a long subtitle list cannot run off the edge —
            // which matters most on a 4:3 screen.
            delegate: Row {
                id: metaRow
                width: root.sw * 0.76875
                spacing: root.sw * 0.0292969

                Text {
                    id: metaLabel
                    text: modelData.label + ":"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333
                }
                Text {
                    text: modelData.value
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333
                    width: metaRow.width - metaLabel.width - metaRow.spacing
                    wrapMode: Text.WordWrap
                }
                }
            }
        }
    }

    // Anchored to the bottom rather than sitting in the content flow, so it
    // stays put however many metadata rows a file produces.
    Rectangle {
        id: playButton
        width: root.sw * 0.5
        height: root.sh * 0.0833333
        color: root.accentColor
        radius: root.sh * 0.0083333

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.175
        anchors.leftMargin: root.sw * 0.115625

        visible: (item.resourceUri || "") !== ""

        Text {
            text: view.resumeMs > 0
                  ? "▶ Resume (" + formatDuration(view.resumeMs) + ")"
                  : "▶ Play"
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0416667
            font.bold: true
            anchors.centerIn: parent
        }

        MouseArea {
            anchors.fill: parent
            onClicked: view.startPlayback(view.resumeMs)
        }
    }

    // Footer
    Text {
        text: root.hints.back + ":BACK "
              + (view.resumeMs > 0 ? root.hints.browse + ":FROM START " : "")
              + root.hints.select + ":PLAY"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }

    Keys.onReturnPressed: view.startPlayback(view.resumeMs)
    Keys.onRightPressed: view.startPlayback(0)

    // Read once on load: getResumePosition() is a plain call, not a binding.
    property int resumeMs: 0

    // Playback lives in Player.qml — it owns the mpv key forwarding and writes
    // the resume position back when mpv exits.
    function startPlayback(startMs) {
        if (!item.resourceUri)
            return

        // Only possible once the probe has landed; otherwise pass 0 and let
        // Player.qml fall back to mpv's --alang/--slang matching.
        var audioTrack = 0, subTrack = 0
        if (probed) {
            audioTrack = pickTrack(probed.audio,
                                   appCore.get_setting(moduleRoot.moduleId, "audio_lang") || "-",
                                   /commentary|descri/i)
            subTrack = pickTrack(probed.subtitles,
                                 appCore.get_setting(moduleRoot.moduleId, "sub_lang") || "-",
                                 /sign|song|forced|commentary/i)
        }

        view.navigateTo("Player.qml", {
            url: item.resourceUri,
            title: item.title,
            objectId: item.id,
            startMs: startMs || 0,
            subtitleUri: item.subtitleUri || "",
            audioTrack: audioTrack,
            subTrack: subTrack
        }, {})
    }

    // Built once from the item rather than bound per row.
    // Filled in by dlnaBackend.probeTracks() a moment after this view opens.
    property var probed: null

    readonly property var metaRows: {
        var rows = []
        var d = formatDlnaDuration(item.duration)
        if (d !== "") rows.push({ label: "Duration", value: d })

        var q = qualityLabel(item.resolution)
        if (q !== "") rows.push({ label: "Quality", value: q })

        // Real track languages once the probe returns, falling back to the
        // channel count DLNA gave us.
        var a = probed ? trackLabel(probed.audio, true)
                       : audioLabel(item.audioChannels, item.sampleRate)
        if (a !== "") rows.push({ label: "Audio", value: a })

        var subParts = []
        if (probed && probed.subtitles && probed.subtitles.length > 0)
            subParts.push(trackLabel(probed.subtitles, false))
        if (item.subtitleUri)
            subParts.push("External file")
        if (subParts.length > 0)
            rows.push({ label: "Subtitles", value: subParts.join(" \u2022 ") })

        if (item.size > 0) rows.push({ label: "Size", value: formatFileSize(item.size) })
        return rows
    }

    // DLNA reports "1:54:41.875"; the fractional seconds are noise here.
    // ISO 639-2 codes are what files actually carry; show something readable.
    readonly property var languageNames: ({
        "eng": "English",  "spa": "Spanish",  "jpn": "Japanese", "fra": "French",
        "fre": "French",   "deu": "German",   "ger": "German",   "ita": "Italian",
        "por": "Portuguese", "rus": "Russian", "kor": "Korean",  "zho": "Chinese",
        "chi": "Chinese",  "nld": "Dutch",    "dut": "Dutch",    "swe": "Swedish",
        "pol": "Polish",   "tur": "Turkish",  "ara": "Arabic",   "hin": "Hindi",
        "und": "Unknown"
    })

    function languageName(code) {
        if (!code) return "Unknown"
        var c = String(code).toLowerCase()
        return languageNames[c] || c.toUpperCase()
    }

    function channelLabel(n) {
        return n === 1 ? "Mono" : n === 2 ? "Stereo"
             : n === 6 ? "5.1"  : n === 8 ? "7.1" : n + "ch"
    }

    // One entry per track for audio; subtitles collapse to unique languages,
    // since a file often carries several in the same language.
    function trackLabel(tracks, withChannels) {
        if (!tracks || tracks.length === 0) return ""
        var out = []
        for (var i = 0; i < tracks.length; i++) {
            var name = languageName(tracks[i].language)
            if (withChannels && tracks[i].channels > 0)
                name += " " + channelLabel(tracks[i].channels)
            if (out.indexOf(name) === -1) out.push(name)
        }
        return out.join(" \u2022 ")
    }

    Connections {
        target: dlnaBackend
        function onTracksProbed(tracks) { view.probed = tracks }
    }

    // mpv numbers --aid/--sid per stream type, 1-based, so the ordinal within
    // the probed list is what it wants — not ffprobe's global stream index.
    // Returns 0 for "no opinion", which leaves mpv to decide.
    function pickTrack(tracks, wantLang, avoidPattern) {
        if (!tracks || tracks.length === 0)
            return 0

        var pool = []
        for (var i = 0; i < tracks.length; i++) {
            var lang = String(tracks[i].language || "").toLowerCase()
            if (wantLang === "-" || wantLang === "" || lang === wantLang)
                pool.push({ track: tracks[i], ordinal: i + 1 })
        }
        if (pool.length === 0)
            return 0

        // Prefer a full track: "Signs & Songs", forced and commentary tracks
        // only cover part of the content.
        for (var j = 0; j < pool.length; j++) {
            if (!avoidPattern.test(String(pool[j].track.title || "")))
                return pool[j].ordinal
        }
        return pool[0].ordinal
    }

    function formatDlnaDuration(raw) {
        if (!raw) return ""
        var t = String(raw).split(".")[0]
        var p = t.split(":")
        if (p.length !== 3) return t
        var h = parseInt(p[0]), m = parseInt(p[1]), s = parseInt(p[2])
        var mm = String(m).padStart(2, '0')
        var ss = String(s).padStart(2, '0')
        return h > 0 ? h + ":" + mm + ":" + ss : m + ":" + ss
    }

    function qualityLabel(res) {
        if (!res) return ""
        var p = String(res).split("x")
        if (p.length !== 2) return String(res)
        var h = parseInt(p[1])
        var name = h >= 2160 ? "4K"
                 : h >= 1440 ? "1440p"
                 : h >= 1080 ? "1080p"
                 : h >= 720  ? "720p"
                 : h >= 576  ? "576p"
                 : h >= 480  ? "480p"
                 : h + "p"
        return name
    }

    function audioLabel(channels, sampleRate) {
        var bits = []
        if (channels > 0) {
            bits.push(channels === 1 ? "Mono"
                    : channels === 2 ? "Stereo"
                    : channels === 6 ? "5.1"
                    : channels === 8 ? "7.1"
                    : channels + " channels")
        }
        if (sampleRate > 0) bits.push(Math.round(sampleRate / 1000) + " kHz")
        return bits.join(" \u2022 ")
    }

    function formatDuration(ms) {
        if (ms <= 0) return "0:00"
        var total   = Math.floor(ms / 1000)
        var hours   = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var seconds = total % 60
        var mm = String(minutes).padStart(2, '0')
        var ss = String(seconds).padStart(2, '0')
        return hours > 0 ? hours + ":" + mm + ":" + ss : minutes + ":" + ss
    }

    function formatFileSize(bytes) {
        if (bytes === 0) return "0 B"
        const k = 1024
        const sizes = ["B", "KB", "MB", "GB"]
        const i = Math.floor(Math.log(bytes) / Math.log(k))
        return Math.round(bytes / Math.pow(k, i) * 100) / 100 + " " + sizes[i]
    }

    Component.onCompleted: {
        if (item.id)
            resumeMs = dlnaBackend.getResumePosition(item.id)
        if (item.resourceUri)
            dlnaBackend.probeTracks(item.resourceUri)
    }
}
