import QtQuick
import Components

FocusScope {
    id: detailRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var item: navParams.item || {}
    property string libraryName: navParams.libraryName || ""

    // Loaded detail from backend
    property var detail: null

    // Extras attached to this item (trailers, deleted scenes, …)
    property var extras: []
    readonly property bool hasExtras: extras.length > 0

    // Displayed name — also used as the Extras view's header subtitle
    readonly property string displayName: {
        var base = (item.type === "episode" && item.grandparentTitle)
                   ? item.grandparentTitle : item.title
        return item.editionTitle ? base + " (" + item.editionTitle + ")" : base
    }

    // Focus rows: 0=play button, 1=extras (when hasExtras), 2=audio, 3=subtitles
    property int focusRow: 0

    // Ordered list of currently reachable focus rows
    function activeRows() {
        var rows = [0]
        if (hasExtras) rows.push(1)
        if (detail && detail.audioStreams && detail.audioStreams.length > 0) rows.push(2)
        if (detail && detail.subtitleStreams && detail.subtitleStreams.length > 1) rows.push(3)
        return rows
    }

    function stepFocus(dir) {
        var rows = activeRows()
        var i = rows.indexOf(focusRow)
        if (i === -1) { focusRow = 0; return }
        var next = i + dir
        if (next >= 0 && next < rows.length) focusRow = rows[next]
    }

    // True from when PLAY is pressed until we navigate to the Player (or error
    // out). Plex can take a few seconds to hand back a stream/transcode URL, so
    // we show a LOADING overlay instead of leaving the screen looking frozen.
    property bool isLaunching: false

    // Current stream selections (indices into stream lists)
    property int audioIdx: 0
    property int subtitleIdx: 0

    // Session ID for the current playback instance. Regenerated on every play
    // (see Keys.onReturnPressed): reusing one lets Plex hand back a stale
    // transcode session built with the previously selected audio/subtitle.
    property string sessionId: newSessionId()

    function newSessionId() {
        var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        var id = ""
        for (var i = 0; i < 12; i++) id += chars[Math.floor(Math.random() * chars.length)]
        return id
    }

    function durationStr(ms) {
        if (!ms) return ""
        var totalMin = Math.floor(ms / 60000)
        var h = Math.floor(totalMin / 60)
        var m = totalMin % 60
        if (h > 0) return h + "HR:" + (m < 10 ? "0" : "") + m + "MIN"
        return m + "MIN"
    }

    Connections {
        target: plexBackend

        function onItemLoaded(d) {
            detailRoot.detail = d
            // Set initial stream indices
            detailRoot.audioIdx = 0
            detailRoot.subtitleIdx = 0
            if (d.audioStreams) {
                for (var i = 0; i < d.audioStreams.length; i++) {
                    if (d.audioStreams[i].id === d.selectedAudioId) { detailRoot.audioIdx = i; break }
                }
            }
            if (d.subtitleStreams) {
                for (var j = 0; j < d.subtitleStreams.length; j++) {
                    if (d.subtitleStreams[j].id === d.selectedSubtitleId) { detailRoot.subtitleIdx = j; break }
                }
            }
        }

        function onStreamUrlReady(url, plexToken) {
            if (!detailRoot.detail) return
            var d = detailRoot.detail
            var audioId = d.audioStreams && d.audioStreams[detailRoot.audioIdx]
                ? d.audioStreams[detailRoot.audioIdx].id : ""
            var subId = d.subtitleStreams && d.subtitleStreams[detailRoot.subtitleIdx]
                ? d.subtitleStreams[detailRoot.subtitleIdx].id : "0"
            var subUrl = (d.subtitleStreams && d.subtitleStreams[detailRoot.subtitleIdx])
                ? (d.subtitleStreams[detailRoot.subtitleIdx].subUrl || "") : ""

            var imageSubs = []
            if (d.subtitleStreams) {
                for (var k = 0; k < d.subtitleStreams.length; k++) {
                    if (d.subtitleStreams[k].imageSubtitle) imageSubs.push(d.subtitleStreams[k].id)
                }
            }

            detailRoot.navigateTo("Player.qml", {
                streamUrl: url,
                plexToken: plexToken,
                ratingKey: d.ratingKey,
                partKey: d.partKey,
                partId: d.partId,
                title: d.title,
                viewOffset: d.viewOffset || 0,
                duration: d.duration || 0,
                audioStreams: d.audioStreams || [],
                subtitleStreams: d.subtitleStreams || [],
                selectedAudioId: audioId,
                selectedSubtitleId: subId,
                selectedSubtitleUrl: subUrl,
                sessionId: detailRoot.sessionId,
                isTranscoding: d.forceTranscode || false,
                imageSubtitleIds: imageSubs
            }, {})
        }

        function onExtrasLoaded(items) {
            detailRoot.extras = items
        }

        function onErrorOccurred(msg) {
            console.log("[Item] Error: " + msg)
            detailRoot.isLaunching = false
        }
    }

    Component.onCompleted: {
        if (item.ratingKey) {
            plexBackend.load_item_detail(item.ratingKey)
            plexBackend.load_extras(item.ratingKey)
        }
        focusRow = 0
    }

    focus: true

    Keys.onUpPressed: {
        if (isLaunching) return
        stepFocus(-1)
    }
    Keys.onDownPressed: {
        if (isLaunching) return
        stepFocus(1)
    }
    Keys.onLeftPressed: {
        if (isLaunching) return
        if (!detail) return
        if (focusRow === 2 && detail.audioStreams && detail.audioStreams.length > 1)
            audioIdx = (audioIdx - 1 + detail.audioStreams.length) % detail.audioStreams.length
        else if (focusRow === 3 && detail.subtitleStreams && detail.subtitleStreams.length > 1)
            subtitleIdx = (subtitleIdx - 1 + detail.subtitleStreams.length) % detail.subtitleStreams.length
    }
    Keys.onRightPressed: {
        if (isLaunching) return
        if (!detail) return
        if (focusRow === 2 && detail.audioStreams && detail.audioStreams.length > 1)
            audioIdx = (audioIdx + 1) % detail.audioStreams.length
        else if (focusRow === 3 && detail.subtitleStreams && detail.subtitleStreams.length > 1)
            subtitleIdx = (subtitleIdx + 1) % detail.subtitleStreams.length
    }
    Keys.onReturnPressed: {
        if (isLaunching) return
        if (focusRow === 1 && hasExtras) {
            navigateTo("Extras.qml", {
                extras: extras,
                ratingKey: item.ratingKey,
                itemTitle: displayName,
                libraryName: libraryName
            }, {})
            return
        }
        if (focusRow === 0 && detail) {
            // Show the loading overlay immediately; clears on navigate or error.
            isLaunching = true
            var audioId = detail.audioStreams && detail.audioStreams[audioIdx]
                ? detail.audioStreams[audioIdx].id : ""
            var subId = detail.subtitleStreams && detail.subtitleStreams[subtitleIdx]
                ? detail.subtitleStreams[subtitleIdx].id : "0"

            // Persist the picked tracks to Plex so they survive returning to
            // this screen, and so a transcode burns the streams the user chose
            // (the server selects from its stored default, not just inline
            // params). subtitleStreamID "0" disables subtitles.
            if (detail.partId) {
                if (audioId) plexBackend.set_audio_stream(audioId, detail.partId)
                plexBackend.set_subtitle_stream(subId, detail.partId)
            }

            // Fresh session per play so Plex builds a new transcode for this
            // exact selection instead of reusing the prior one.
            sessionId = newSessionId()

            if (detail.forceTranscode) {
                // Always transcode from the start so the full timeline is seekable.
                // The Player resumes by seeking mpv to viewOffset (see doStartPlayback),
                // which lets the user rewind past the resume point.
                plexBackend.request_transcode(detail.ratingKey, detail.partKey, sessionId, audioId, subId, 0)
            } else {
                plexBackend.build_stream_url(detail.ratingKey, detail.partKey, sessionId)
            }
        }
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
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

            Column {
                width: root.sw * 0.1875 //120

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

                // Extras Button
                Rectangle {
                    id: extrasButton
                    visible: detailRoot.hasExtras
                    color: focusRow === 1 ? root.accentColor : "transparent"
                    width: parent.width
                    height: extrasLabel.implicitHeight + root.sh * 0.025 //12
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        id: extrasLabel
                        text: "VIEW EXTRAS"
                        color: focusRow === 1 ? root.surfaceColor : root.secondaryColor
                        font.family: root.globalFont
                        anchors.centerIn: parent
                        font.pixelSize: root.sh * 0.025 //12
                    }
                }
            }

            Column {
                topPadding: root.sh * 0.0083333 //4
                width: root.sw * 0.54375 //348
                spacing: root.sh * 0.0166667 //8

                //Name
                Text {
                    text: detailRoot.displayName
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
                            var sNum = (item.parentIndex != null) ? item.parentIndex
                                       : ((detail.parentIndex != null) ? detail.parentIndex : "?")
                            var eNum = item.index || detail.index || "?"
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
                        text: detail ? detail.summary : ""
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
                color: focusRow === 2 ? root.accentColor : "transparent"
            }

            Text {
                text: "Audio"
                color: focusRow === 2 ? root.surfaceColor : root.primaryColor
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
                    color: focusRow === 2 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                }
                Text {
                    text: (detail && detail.audioStreams && detail.audioStreams[audioIdx])
                          ? detail.audioStreams[audioIdx].displayTitle : ""
                    color: focusRow === 2 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize:root.sh * 0.0416667 //20
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

        // SUBTITLES row
        Item {
            id: subtitleRow
            visible: detail && detail.subtitleStreams && detail.subtitleStreams.length > 1
            anchors.top: audioRow.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: root.sh * 0.0583333 //28

            Rectangle {
                anchors.fill: parent
                color: focusRow === 3 ? root.accentColor : "transparent"
            }

            Text {
                text: "Subtitles"
                color: focusRow === 3 ? root.surfaceColor : root.primaryColor
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
                    color: focusRow === 3 ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                }
                Text {
                    text: (detail && detail.subtitleStreams && detail.subtitleStreams[subtitleIdx])
                          ? detail.subtitleStreams[subtitleIdx].displayTitle : ""
                    color: focusRow === 3 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize:root.sh * 0.0416667 //20
                }
                Text {
                    text: "\u25BA"
                    color: focusRow === 3 ? root.surfaceColor : root.tertiaryColor
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

    // Launch overlay — covers the detail screen while Plex prepares the stream
    // so a slow server doesn't make the app look frozen after pressing PLAY.
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
