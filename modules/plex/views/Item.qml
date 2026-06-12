import QtQuick
import Components

FocusScope {
    id: detailRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()

    property var item: navParams.item || {}
    property string libraryName: navParams.libraryName || ""

    // Loaded detail from backend
    property var detail: null

    // Focus rows: 0=play button, 1=audio, 2=subtitles
    property int focusRow: 0

    // Current stream selections (indices into stream lists)
    property int audioIdx: 0
    property int subtitleIdx: 0

    // Unique session ID for this playback instance
    readonly property string sessionId: {
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
            })
        }

        function onErrorOccurred(msg) {
            console.log("[Item] Error: " + msg)
        }
    }

    Component.onCompleted: {
        if (item.ratingKey) plexBackend.load_item_detail(item.ratingKey)
        focusRow = 0
    }

    focus: true

    Keys.onUpPressed: {
        if (focusRow > 0) focusRow--
    }
    Keys.onDownPressed: {
        if (detail) {
            var maxRow = 0
            if (detail.audioStreams && detail.audioStreams.length > 0) maxRow = 1
            if (detail.subtitleStreams && detail.subtitleStreams.length > 1) maxRow = 2
            if (focusRow < maxRow) focusRow++
        }
    }
    Keys.onLeftPressed: {
        if (!detail) return
        if (focusRow === 1 && detail.audioStreams && detail.audioStreams.length > 1)
            audioIdx = (audioIdx - 1 + detail.audioStreams.length) % detail.audioStreams.length
        else if (focusRow === 2 && detail.subtitleStreams && detail.subtitleStreams.length > 1)
            subtitleIdx = (subtitleIdx - 1 + detail.subtitleStreams.length) % detail.subtitleStreams.length
    }
    Keys.onRightPressed: {
        if (!detail) return
        if (focusRow === 1 && detail.audioStreams && detail.audioStreams.length > 1)
            audioIdx = (audioIdx + 1) % detail.audioStreams.length
        else if (focusRow === 2 && detail.subtitleStreams && detail.subtitleStreams.length > 1)
            subtitleIdx = (subtitleIdx + 1) % detail.subtitleStreams.length
    }
    Keys.onReturnPressed: {
        if (focusRow === 0 && detail) {
            if (detail.forceTranscode) {
                var audioId = detail.audioStreams && detail.audioStreams[audioIdx]
                    ? detail.audioStreams[audioIdx].id : ""
                var subId = detail.subtitleStreams && detail.subtitleStreams[subtitleIdx]
                    ? detail.subtitleStreams[subtitleIdx].id : "0"
                plexBackend.request_transcode(detail.ratingKey, detail.partKey, sessionId, audioId, subId, detail.viewOffset || 0)
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

                //Name
                Text {
                    text: {
                        var base = (item.type === "episode" && item.grandparentTitle)
                                   ? item.grandparentTitle : item.title
                        return item.editionTitle ? base + " (" + item.editionTitle + ")" : base
                    }
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
                Text {
                    text: (detail && detail.audioStreams && detail.audioStreams[audioIdx])
                          ? detail.audioStreams[audioIdx].displayTitle : ""
                    color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize:root.sh * 0.0416667 //20
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
            visible: detail && detail.subtitleStreams && detail.subtitleStreams.length > 1
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
                Text {
                    text: (detail && detail.subtitleStreams && detail.subtitleStreams[subtitleIdx])
                          ? detail.subtitleStreams[subtitleIdx].displayTitle : ""
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
}
