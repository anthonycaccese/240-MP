import QtQuick
import Components

FocusScope {
    id: extrasRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    // Extras arrive fully-formed via navParams (fetched by the detail view that
    // pushed us), so there is no loading state — each entry is a playable
    // detail (buildItemDetail shape) plus extraTypeLabel. They are re-fetched
    // on load anyway (see Component.onCompleted): the navParams copy is a
    // snapshot whose viewOffsets go stale once an extra is partially played.
    property var extras: navParams.extras || []
    property string itemTitle: navParams.itemTitle || ""
    property string libraryName: navParams.libraryName || ""

    // The extra whose stream is being prepared; set on ENTER, cleared on error.
    // Doubles as the launch guard so stray streamUrlReady signals are ignored.
    property var launchingExtra: null

    // Fresh session per play so Plex builds a new transcode for each launch
    // instead of reusing the prior one (mirrors Item.qml).
    property string sessionId: ""

    function newSessionId() {
        var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        var id = ""
        for (var i = 0; i < 12; i++) id += chars[Math.floor(Math.random() * chars.length)]
        return id
    }

    Connections {
        target: plexBackend

        function onExtrasLoaded(items) {
            // Fresh viewOffsets so replaying a partially-watched extra can
            // resume. Ignore empty results — a transient fetch failure must
            // not blank a list we already have.
            if (!items || items.length === 0) return
            var idx = itemList.currentIndex
            extrasRoot.extras = items
            itemList.currentIndex = Math.min(Math.max(idx, 0), items.length - 1)
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
        }

        function onStreamUrlReady(url, plexToken) {
            var d = extrasRoot.launchingExtra
            if (!d) return

            var subId = "0", subUrl = ""
            if (d.subtitleStreams) {
                for (var k = 0; k < d.subtitleStreams.length; k++) {
                    if (d.subtitleStreams[k].id === d.selectedSubtitleId) {
                        subId = d.subtitleStreams[k].id
                        subUrl = d.subtitleStreams[k].subUrl || ""
                        break
                    }
                }
            }
            var imageSubs = []
            if (d.subtitleStreams) {
                for (var m = 0; m < d.subtitleStreams.length; m++) {
                    if (d.subtitleStreams[m].imageSubtitle) imageSubs.push(d.subtitleStreams[m].id)
                }
            }

            extrasRoot.navigateTo("Player.qml", {
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
                selectedAudioId: d.selectedAudioId || "",
                selectedSubtitleId: subId,
                selectedSubtitleUrl: subUrl,
                sessionId: extrasRoot.sessionId,
                isTranscoding: d.forceTranscode || false,
                imageSubtitleIds: imageSubs,
                allowAutoplay: false
            }, { currentIndex: itemList.currentIndex })
        }

        function onErrorOccurred(msg) {
            console.log("[Extras] Error: " + msg)
            extrasRoot.launchingExtra = null
        }
    }

    Component.onCompleted: {
        if (extras.length > 0) {
            var st = navParams.navListState || {}
            itemList.currentIndex = Math.min(st.currentIndex ?? 0, extras.length - 1)
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
        }
        if (navParams.ratingKey) plexBackend.load_extras(navParams.ratingKey)
    }

    focus: true

    Keys.onUpPressed: {
        if (launchingExtra || extras.length === 0) return
        if (itemList.currentIndex > 0) itemList.currentIndex--
        else itemList.currentIndex = extras.length - 1
        itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
    }
    Keys.onDownPressed: {
        if (launchingExtra || extras.length === 0) return
        if (itemList.currentIndex < extras.length - 1) itemList.currentIndex++
        else itemList.currentIndex = 0
        itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
    }
    Keys.onReturnPressed: {
        if (launchingExtra) return
        var d = extras[itemList.currentIndex]
        if (!d) return
        launchingExtra = d
        sessionId = newSessionId()
        // No track persistence here: extras play with the server's default
        // audio/subtitle selection (there is no track UI on this screen).
        if (d.forceTranscode) {
            // Always transcode from the start so the full timeline is seekable;
            // the Player resumes by seeking mpv to viewOffset (see Item.qml).
            plexBackend.request_transcode(d.ratingKey, d.partKey, sessionId,
                                          d.selectedAudioId || "",
                                          d.selectedSubtitleId || "0", 0)
        } else {
            plexBackend.build_stream_url(d.ratingKey, d.partKey, sessionId)
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

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: itemTitle
    }

    // List
    ListView {
        id: itemList
        model: extras
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: true

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.075 //36

            // Full-width background highlight for the active row
            Rectangle {
                color: root.accentColor
                anchors.fill: parent
                visible: itemList.currentIndex === index
            }

            // Vertical stack for Type and Title
            Column {
                id: textColumn
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.0109375 //7
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.sh * 0.0041667 //2

                Text {
                    id: extraType
                    text: modelData.extraTypeLabel || ""
                    color: itemList.currentIndex === index ? root.surfaceColor : root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0208333 //10
                }

                // Clipped title with marquee scroll when it overflows the row
                Item {
                    id: extraTitleClip
                    width: Math.min(extraTitle.implicitWidth, itemList.width - root.sw * 0.021875) //14
                    height: extraTitle.implicitHeight
                    clip: true

                    Text {
                        id: extraTitle
                        text: modelData.title || ""
                        color: itemList.currentIndex === index ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0333333 //16
                        x: 0
                    }

                    SequentialAnimation {
                        running: (itemList.currentIndex === index) &&
                                 (extraTitle.implicitWidth > extraTitleClip.width)
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) extraTitle.x = 0
                        PauseAnimation { duration: 1500 }
                        NumberAnimation {
                            target: extraTitle; property: "x"
                            to: extraTitleClip.width - extraTitle.implicitWidth
                            duration: Math.abs(to) * 20
                        }
                        PauseAnimation { duration: 2000 }
                        PropertyAction { target: extraTitle; property: "x"; value: 0 }
                    }
                }
            }
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // Launch overlay — covers the list while Plex prepares the stream so a
    // slow server doesn't make the app look frozen after pressing SELECT.
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: launchingExtra !== null
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
