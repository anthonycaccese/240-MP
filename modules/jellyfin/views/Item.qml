import QtQuick
import Components

FocusScope {
    id: detailRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var item: navParams.item || {}
    property string libraryName: navParams.libraryName || ""

    // Loaded detail from backend
    property var detail: null

    // Focus rows: 0=play button, 1=audio, 2=subtitles
    property int focusRow: 0

    // True from when PLAY is pressed until we navigate to the Player (or error out).
    property bool isLaunching: false

    // Current stream selections
    // audioIdx is a 0-based index into detail.audioStreams.
    // subtitleIdx is 0 for Off, otherwise a 1-based index into detail.subtitleStreams
    // (so detail.subtitleStreams[subtitleIdx - 1] is the active subtitle).
    property int audioIdx: 0
    property int subtitleIdx: 0

    function durationStr(ms) {
        if (!ms) return ""
        var totalMin = Math.floor(ms / 60000)
        var h = Math.floor(totalMin / 60)
        var m = totalMin % 60
        if (h > 0) return h + "HR:" + (m < 10 ? "0" : "") + m + "MIN"
        return m + "MIN"
    }

    function selectedAudioId() {
        if (!detail || !detail.audioStreams || detail.audioStreams.length === 0) return ""
        return detail.audioStreams[audioIdx].id || ""
    }

    function selectedSubtitleId() {
        if (subtitleIdx === 0) return ""
        if (!detail || !detail.subtitleStreams || detail.subtitleStreams.length === 0) return ""
        return detail.subtitleStreams[subtitleIdx - 1].id || ""
    }

    function selectedStreamIdx(type) {
        var id = (type === "audio") ? selectedAudioId() : selectedSubtitleId()
        return id ? parseInt(id) : -1
    }

    function selectedAudioIndex() {
        if (!detail || !detail.audioStreams || detail.audioStreams.length === 0) return -1
        return audioIdx
    }

    function selectedSubtitleIndex() {
        if (subtitleIdx === 0) return -1
        if (!detail || !detail.subtitleStreams || detail.subtitleStreams.length === 0) return -1
        return subtitleIdx - 1
    }

    // Persisted selection so onItemLoaded doesn't wipe it
    property bool hasRestoredState: false
    // Set true when the user manually changes a track via arrow keys, so the
    // async onServerLanguagePreferencesReady doesn't override their choice.
    property bool userChangedTracks: false

    // Server-side language preferences — fetched on first load as fallback
    property string serverAudioLang: ""
    property string serverSubLang: ""
    property string serverSubMode: ""   // Default | Smart | Always | OnlyForced | None
    property bool serverPrefsLoaded: false

    Connections {
        target: jellyfinBackend

        function onItemLoaded(d) {
            detailRoot.detail = d
            // Fetch server-side preferences on first load
            if (!serverPrefsLoaded) {
                serverPrefsLoaded = true
                jellyfinBackend.load_server_preferences()
            }
            // Only apply defaults if not already restored from Player return
            if (!hasRestoredState) {
                detailRoot.audioIdx = 0
                detailRoot.subtitleIdx = 0
            }
            detailRoot.userChangedTracks = false
        }

        function onServerLanguagePreferencesReady(audioLang, subLang, subMode) {
            serverAudioLang = audioLang
            serverSubLang   = subLang
            serverSubMode   = subMode
            // Only apply server prefs if the user hasn't manually changed a track
            // (otherwise the async response would override their arrow-key choice).
            if (detailRoot.detail && !hasRestoredState && !detailRoot.userChangedTracks) {
                detailRoot.applyLanguagePreferences(detailRoot.detail)
            }
        }

        function onStreamUrlReady(url) {
            if (!detailRoot.detail) return
            var d = detailRoot.detail

            detailRoot.navigateTo("Player.qml", {
                streamUrl: url,
                itemId: d.itemId,
                seriesId: d.seriesId || "",
                mediaSourceId: d.mediaSourceId || d.itemId,
                title: d.title,
                viewOffset: d.viewOffset || 0,
                audioStreams: d.audioStreams || [],
                subtitleStreams: d.subtitleStreams || [],
                selectedAudioId: detailRoot.selectedAudioId(),
                selectedSubtitleId: detailRoot.selectedSubtitleId(),
                parentIndex: d.parentIndex || 0,
                index: d.index || 0
            }, { subtitleIdx: subtitleIdx, audioIdx: audioIdx })
        }

        function onErrorOccurred(msg) {
            console.log("[Jellyfin Item] Error: " + msg)
            detailRoot.isLaunching = false
        }
    }

    Component.onCompleted: {
        if (item.itemId) jellyfinBackend.load_item_detail(item.itemId)
        focusRow = 0
        // Restore previous audio/subtitle selection when returning from Player
        if (navListState.subtitleIdx !== undefined || navListState.audioIdx !== undefined) {
            subtitleIdx = navListState.subtitleIdx !== undefined ? navListState.subtitleIdx : 0
            audioIdx = navListState.audioIdx !== undefined ? navListState.audioIdx : 0
            hasRestoredState = true
        }
    }

    focus: true

    Keys.onUpPressed: {
        if (isLaunching) return
        if (focusRow > 0) focusRow--
    }
    Keys.onDownPressed: {
        if (isLaunching) return
        if (detail) {
            var maxRow = 0
            if (detail.audioStreams && detail.audioStreams.length > 0) maxRow = 1
            if (detail.subtitleStreams && detail.subtitleStreams.length > 0) maxRow = 2
            if (focusRow < maxRow) focusRow++
        }
    }
    // Debounce: batch rapid arrow-key changes into a single backend save
    // to reduce QML→C++ cross-context calls that can crash on Pi.
    Timer {
        id: debounceSaveTimer
        interval: 200
        repeat: false
        onTriggered: _saveCurrentLangs()
    }

    Keys.onLeftPressed: {
        if (isLaunching) return
        if (!detail) return
        if (focusRow === 1 && detail.audioStreams && detail.audioStreams.length > 1) {
            audioIdx = (audioIdx - 1 + detail.audioStreams.length) % detail.audioStreams.length
            userChangedTracks = true
            debounceSaveTimer.restart()
        } else if (focusRow === 2 && detail.subtitleStreams && detail.subtitleStreams.length > 0) {
            subtitleIdx = (subtitleIdx - 1 + (detail.subtitleStreams.length + 1)) % (detail.subtitleStreams.length + 1)
            userChangedTracks = true
            debounceSaveTimer.restart()
        }
    }
    Keys.onRightPressed: {
        if (isLaunching) return
        if (!detail) return
        if (focusRow === 1 && detail.audioStreams && detail.audioStreams.length > 1) {
            audioIdx = (audioIdx + 1) % detail.audioStreams.length
            userChangedTracks = true
            debounceSaveTimer.restart()
        } else if (focusRow === 2 && detail.subtitleStreams && detail.subtitleStreams.length > 0) {
            subtitleIdx = (subtitleIdx + 1) % (detail.subtitleStreams.length + 1)
            userChangedTracks = true
            debounceSaveTimer.restart()
        }
    }
    Keys.onReturnPressed: {
        if (isLaunching) return
        if (focusRow === 0 && detail) {
            isLaunching = true
            // Save the current track selection before playback starts so it
            // persists to the next item via load_server_preferences().
            _saveCurrentLangs()
            // get_playback_url() reports the playback Start to the server once
            // PlaybackInfo resolves (so session id + play method are correct).
            jellyfinBackend.get_playback_url(detail.itemId, detail.mediaSourceId || detail.itemId,
                                             detailRoot.selectedStreamIdx("audio"),
                                             detailRoot.selectedStreamIdx("subtitle"))
        }
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            _saveCurrentLangs()
            goBack()
            event.accepted = true
        }
    }

    // Safety net: save the current selection when the view is destroyed
    // (navigating back), so the cached language is available for the next item.
    Component.onDestruction: _saveCurrentLangs()

    // ------------------------------------------------------------------
    // Language display — uses Jellyfin's own DisplayTitle when available
    // ------------------------------------------------------------------

    function streamLabel(stream) {
        if (!stream) return ""
        if (stream.displayTitle) return stream.displayTitle
        if (stream.title) return stream.title
        return stream.language || stream.codec || ""
    }

    // ------------------------------------------------------------------
    // Language preferences
    // ------------------------------------------------------------------

    // Returns the 1-based subtitleIdx (0 = off) for the first stream matching pred.
    function firstSub(subs, pred) {
        for (var i = 0; i < subs.length; i++) {
            if (pred(subs[i])) return i + 1
        }
        return 0
    }

    // Mirrors Jellyfin's MediaStreamSelector.GetDefaultSubtitleStreamIndex so the
    // preselected track matches what a native client would pick for the user's
    // server-side SubtitleMode. Returns a 1-based subtitleIdx (0 = off).
    function pickSubtitle(subs, prefLang, mode, audioLang) {
        if (!subs || subs.length === 0) return 0
        if (mode === "None") return 0

        var idx = 0
        if (mode === "Smart") {
            // Embedded forced/default first; otherwise load preferred-language subs
            // only when the spoken audio differs from the preferred language.
            idx = firstSub(subs, function(s) { return s.forced || s.selected })
            if (idx === 0 && prefLang && audioLang !== prefLang)
                idx = firstSub(subs, function(s) { return !s.forced && s.language === prefLang })
        } else if (mode === "Always") {
            idx = firstSub(subs, function(s) { return !s.forced && s.language === prefLang })
        } else if (mode === "OnlyForced") {
            idx = firstSub(subs, function(s) { return s.forced })
        } else {
            // "Default" (and any empty/unknown value): embedded forced/default only.
            idx = firstSub(subs, function(s) { return s.forced || s.selected })
        }

        // Final fallback (all modes except None): forced subs matching the audio language.
        if (idx === 0)
            idx = firstSub(subs, function(s) { return s.forced && s.language === audioLang })
        return idx
    }

    function _saveCurrentLangs() {
        if (!detail) return
        // Audio: save language and the position of this stream within
        // streams of the same language, so we can pick the exact track
        // when multiple share a language (e.g. English main + commentary).
        var aStream = (detail.audioStreams && detail.audioStreams[audioIdx])
                      ? detail.audioStreams[audioIdx] : null
        var aLang = aStream ? (aStream.language || "") : ""
        var aLangIdx = -1
        if (aLang && detail.audioStreams) {
            for (var ai = 0; ai < detail.audioStreams.length; ai++) {
                if (detail.audioStreams[ai].language === aLang) {
                    aLangIdx++
                    if (ai === audioIdx) break
                }
            }
        }
        // Subtitle: same approach. subtitleIdx === 0 means Off.
        var sLang = ""
        var sLangIdx = -1
        if (subtitleIdx !== 0) {
            var sStream = (detail.subtitleStreams && detail.subtitleStreams[subtitleIdx - 1])
                          ? detail.subtitleStreams[subtitleIdx - 1] : null
            if (sStream) {
                sLang = sStream.language || ""
                if (sLang && detail.subtitleStreams) {
                    for (var si = 0; si < detail.subtitleStreams.length; si++) {
                        if (detail.subtitleStreams[si].language === sLang) {
                            sLangIdx++
                            if (si === subtitleIdx - 1) break
                        }
                    }
                }
            }
        }
        jellyfinBackend.set_last_track_langs(aLang, sLang, aLangIdx, sLangIdx)
    }

    function applyLanguagePreferences(d) {
        if (!d) return
        // [dev] console.log("[Item] applyPrefs serverAudio=" + serverAudioLang + " serverSub=" + serverSubLang +
        // [dev]             " mode=" + serverSubMode +
        // [dev]             " nAudio=" + (d.audioStreams ? d.audioStreams.length : 0) +
        // [dev]             " nSub=" + (d.subtitleStreams ? d.subtitleStreams.length : 0))

        var savedAudioLangIdx = jellyfinBackend.get_last_audio_lang_idx()
        var savedSubLangIdx   = jellyfinBackend.get_last_sub_lang_idx()

        // Audio: server language preference > same-language index match > IsDefault > first.
        var audioLang = ""
        if (d.audioStreams && d.audioStreams.length > 0) {
            var ai = -1
            if (serverAudioLang) {
                // Collect all streams with the target language
                var candidates = []
                for (var i = 0; i < d.audioStreams.length; i++) {
                    if (d.audioStreams[i].language === serverAudioLang)
                        candidates.push(i)
                }
                if (candidates.length > 0) {
                    // Use saved language-group index when valid
                    if (savedAudioLangIdx >= 0 && savedAudioLangIdx < candidates.length)
                        ai = candidates[savedAudioLangIdx]
                    else
                        ai = candidates[0] // fall back to first
                }
            }
            if (ai < 0) {
                for (var k = 0; k < d.audioStreams.length; k++) {
                    if (d.audioStreams[k].selected) { ai = k; break }
                }
            }
            if (ai < 0) ai = 0
            detailRoot.audioIdx = ai
            audioLang = d.audioStreams[ai].language || ""
        }

        // Subtitles: follow the server's SubtitleMode, keyed off the chosen audio,
        // with saved language-group index to pick the exact track.
        var subPick = pickSubtitle(d.subtitleStreams, serverSubLang, serverSubMode, audioLang)
        if (subPick > 0 && serverSubLang) {
            var subCandidates = []
            for (var si = 0; si < d.subtitleStreams.length; si++) {
                if (d.subtitleStreams[si].language === serverSubLang)
                    subCandidates.push(si)
            }
            if (subCandidates.length > 0) {
                if (savedSubLangIdx >= 0 && savedSubLangIdx < subCandidates.length)
                    subPick = subCandidates[savedSubLangIdx] + 1
            }
        }
        detailRoot.subtitleIdx = subPick
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: libraryName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Loading Indicator
    Text {
        visible: !detail
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    Item {
        visible: detail !== null
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true

        Row {
            id: itemDetails
            height: root.sh * 0.35 //168
            spacing: root.sw * 0.0375 //24

            // PLAY / RSUM button
            Rectangle {
                id: playButton
                color: focusRow === 0 ? root.accentColor : root.surfaceColor
                border.color: focusRow === 0 ? root.accentColor : root.tertiaryColor
                width: root.sw * 0.1875 //120
                height: root.sh * 0.1166667 //56
                border.width: root.sh * 0.003125 //2

                Text {
                    anchors.centerIn: parent
                    text: (detail && detail.viewOffset > 0) ? "RSUM \u25BA" : "PLAY \u25BA"
                    color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.05 //24
                }
            }

            Column {
                topPadding: root.sh * 0.0083333 //4
                width: root.sw * 0.54375 //348
                spacing: root.sh * 0.0166667 //8

                // Name
                Text {
                    text: item.title || ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.05 //24
                }

                // Year & Duration / Episode identifier
                Text {
                    text: {
                        if (!detail) return ""
                        if (item.type === "episode") {
                            var sNum = (item.parentIndex != null) ? item.parentIndex : "?"
                            var eNum = (item.index || item.index === 0) ? item.index : "?"
                            return "S" + sNum + "E" + eNum + ": " + item.title
                        }
                        var parts = []
                        if (detail.year) parts.push(String(detail.year))
                        if (detail.duration) parts.push(durationStr(detail.duration))
                        return parts.join(" - ")
                    }
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.0333333 //16
                }

                // Summary
                Item {
                    id: summaryContainer
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: root.sh * 0.1375 //66
                    clip: true

                    Text {
                        id: summaryText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        text: detail ? detail.overview : ""
                        color: root.primaryColor
                        font.family: root.globalFont
                        wrapMode: Text.WordWrap
                        font.pixelSize: root.sh * 0.0291667 //14
                        lineHeight: 1.3
                    }

                    SequentialAnimation {
                        running: detail !== null && summaryText.implicitHeight > summaryContainer.height
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) summaryText.y = 0
                        PauseAnimation { duration: 3000 }
                        NumberAnimation {
                            target: summaryText; property: "y"
                            to: summaryContainer.height - summaryText.implicitHeight
                            duration: Math.abs(to) * 120
                        }
                        PauseAnimation { duration: 4000 }
                        PropertyAction { target: summaryText; property: "y"; value: 0 }
                    }
                }
            }
        }

        // Playback Settings
        Text {
            id: pbSettingsLabel
            text: "Playback Settings:"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.top: itemDetails.bottom
            anchors.topMargin: root.sh * 0.0145833 //7
            leftPadding: root.sw * 0.009375 //6
            rightPadding: root.sw * 0.009375 //6
            font.pixelSize: root.sh * 0.0291667 //14
        }

        // AUDIO row
        Item {
            id: audioRow
            visible: detail && detail.audioStreams && detail.audioStreams.length > 0
            anchors.top: pbSettingsLabel.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: root.sh * 0.0145833 //7
            height: root.sh * 0.0583333 //28

            Rectangle {
                anchors.fill: parent
                color: focusRow === 1 ? root.accentColor : "transparent"
            }

            Text {
                text: "Audio"
                color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.009375 //6
                font.pixelSize: root.sh * 0.0416667 //20
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.009375 //6
                spacing: root.sw * 0.00625 //4

                Text {
                    text: "\u25C4"
                    color: focusRow === 1 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                }
                Item {
                    id: audioValueClip
                    width: root.sw * 0.25 //160
                    height: parent.height
                    clip: true

                    Text {
                        id: audioValueText
                        anchors.verticalCenter: parent.verticalCenter
                        text: (detail && detail.audioStreams && detail.audioStreams[audioIdx])
                              ? streamLabel(detail.audioStreams[audioIdx]) : ""
                        color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0416667 //20
                        x: 0
                    }

                    SequentialAnimation {
                        running: focusRow === 1 && audioValueText.implicitWidth > audioValueClip.width
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) audioValueText.x = 0
                        PauseAnimation { duration: 1500 }
                        NumberAnimation {
                            target: audioValueText; property: "x"
                            to: audioValueClip.width - audioValueText.implicitWidth
                            duration: Math.abs(to) * 20
                        }
                        PauseAnimation { duration: 2000 }
                        PropertyAction { target: audioValueText; property: "x"; value: 0 }
                    }
                }
                Text {
                    text: "\u25BA"
                    color: focusRow === 1 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                }
            }
        }

        // SUBTITLES row
        Item {
            id: subtitleRow
            visible: detail && detail.subtitleStreams && detail.subtitleStreams.length > 0
            anchors.top: audioRow.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: root.sh * 0.0583333 //28

            Rectangle {
                anchors.fill: parent
                color: focusRow === 2 ? root.accentColor : "transparent"
            }

            Text {
                text: "Subtitles"
                color: focusRow === 2 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: root.sw * 0.009375 //6
                font.pixelSize: root.sh * 0.0416667 //20
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.009375 //6
                spacing: root.sw * 0.00625 //4

                Text {
                    text: "\u25C4"
                    color: focusRow === 2 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                }
                Item {
                    id: subtitleValueClip
                    width: root.sw * 0.25 //160
                    height: parent.height
                    clip: true

                    Text {
                        id: subtitleValueText
                        anchors.verticalCenter: parent.verticalCenter
                        text: {
                            if (subtitleIdx === 0) return "Off"
                            if (detail && detail.subtitleStreams && detail.subtitleStreams[subtitleIdx - 1])
                                return streamLabel(detail.subtitleStreams[subtitleIdx - 1])
                            return ""
                        }
                        color: focusRow === 2 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0416667 //20
                        x: 0
                    }

                    SequentialAnimation {
                        running: focusRow === 2 && subtitleValueText.implicitWidth > subtitleValueClip.width
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) subtitleValueText.x = 0
                        PauseAnimation { duration: 1500 }
                        NumberAnimation {
                            target: subtitleValueText; property: "x"
                            to: subtitleValueClip.width - subtitleValueText.implicitWidth
                            duration: Math.abs(to) * 20
                        }
                        PauseAnimation { duration: 2000 }
                        PropertyAction { target: subtitleValueText; property: "x"; value: 0 }
                    }
                }
                Text {
                    text: "\u25BA"
                    color: focusRow === 2 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                }
            }
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.change + ":CHANGE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // Launch overlay — covers the detail screen while the stream URL is prepared.
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: isLaunching
        z: 100

        Text {
            text: "LOADING..."
            color: root.tertiaryColor
            font.family: root.globalFont
            anchors.centerIn: parent
            font.pixelSize: root.sh * 0.05 //24
        }

        Text {
            text: root.hints.back + ":CANCEL"
            color: root.tertiaryColor
            font.family: root.globalFont
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.sh * 0.1041667 //50
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }
}
