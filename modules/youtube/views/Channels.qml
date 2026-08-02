import QtQuick
import Components

// Alphabetical channel list with the A–Z letter-nav panel
// (pattern from modules/plex/views/Items.qml).
FocusScope {
    id: channelsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    property bool letterNavActive: false
    property var letterIndex: []

    function sortKey(title) {
        var t = (title || "").toLowerCase()
        var articles = ["the ", "a ", "an "]
        for (var i = 0; i < articles.length; i++) {
            if (t.indexOf(articles[i]) === 0) { t = t.substring(articles[i].length); break }
        }
        var ch = t.charAt(0).toUpperCase()
        return (ch >= 'A' && ch <= 'Z') ? ch : '#'
    }

    function buildLetterIndex(itemArr) {
        var seen = {}
        var result = []
        for (var i = 0; i < itemArr.length; i++) {
            var letter = sortKey(itemArr[i].title || "")
            if (!seen[letter]) {
                seen[letter] = true
                result.push({ letter: letter, firstIndex: i })
            }
        }
        result.sort(function(a, b) {
            if (a.letter === '#') return -1
            if (b.letter === '#') return 1
            return a.letter < b.letter ? -1 : 1
        })
        return result
    }

    Component.onCompleted: {
        isLoading = true
        errorMessage = ""
        youtubeBackend.load_channels()
    }

    Connections {
        target: youtubeBackend

        function onChannelsLoaded(channels) {
            channelsRoot.isLoading = false
            channelsRoot.items = channels
            channelsRoot.letterIndex = channelsRoot.buildLetterIndex(channels)
            if (channels.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                itemList.currentIndex = Math.min(restore, channels.length - 1)
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            channelsRoot.isLoading = false
            channelsRoot.errorMessage = msg
        }
    }

    focus: true
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
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Channels"
    }

    // Loading / empty / error states
    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage !== ""
        text: errorMessage
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage === "" && items.length === 0
        text: "NO CHANNELS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Channel list
    ListView {
        id: itemList
        model: items
        opacity: letterNavActive ? 0.3 : 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.671875 //430
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) {
                currentIndex--
                var curLetter = sortKey((items[itemList.currentIndex] && items[itemList.currentIndex].title) || "")
                for (var i = 0; i < letterIndex.length; i++) {
                    if (letterIndex[i].letter === curLetter) { letterList.currentIndex = i; break }
                }
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
            }
            else {
                currentIndex = count - 1
                itemList.positionViewAtIndex(currentIndex, ListView.Contain)
                letterList.currentIndex = letterIndex.length - 1
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
            }
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) {
                currentIndex++
                var curLetter = sortKey((items[itemList.currentIndex] && items[itemList.currentIndex].title) || "")
                for (var i = 0; i < letterIndex.length; i++) {
                    if (letterIndex[i].letter === curLetter) { letterList.currentIndex = i; break }
                }
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
            }
            else {
                currentIndex = 0
                itemList.positionViewAtIndex(currentIndex, ListView.Contain)
                letterList.currentIndex = 0
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
            }
        }
        Keys.onReturnPressed: {
            var item = items[itemList.currentIndex]
            if (!item)
                return
            channelsRoot.navigateTo("Subscriptions.qml", {
                mode: "channel",
                channelId: item.channelId,
                channelName: item.title
            }, { currentIndex: itemList.currentIndex })
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                channelsRoot.goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Right && letterIndex.length > 0) {
                var curLetter = sortKey((items[itemList.currentIndex] && items[itemList.currentIndex].title) || "")
                for (var i = 0; i < letterIndex.length; i++) {
                    if (letterIndex[i].letter === curLetter) { letterList.currentIndex = i; break }
                }
                letterNavActive = true
                letterList.forceActiveFocus()
                event.accepted = true
            }
        }

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, itemList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: itemList.currentIndex === index && !letterNavActive
                }

                Text {
                    id: rowText
                    text: modelData.title || ""
                    color: (itemList.currentIndex === index && !letterNavActive)
                       ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
                }

                SequentialAnimation {
                    running: (itemList.currentIndex === index) &&
                             (rowText.implicitWidth > textClip.width)
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) rowText.x = 0
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: rowText; property: "x"
                        to: textClip.width - rowText.implicitWidth
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: rowText; property: "x"; value: 0 }
                }
            }
        }
    }

    // Letter navigation panel
    ListView {
        id: letterList
        model: letterIndex
        visible: letterIndex.length > 0
        opacity: letterNavActive ? 1.0 : 0.3
        anchors.left: itemList.right
        anchors.leftMargin: root.sw * 0.0375 //24
        anchors.top: itemList.top
        width: root.sw * 0.0328125 //21
        height: itemList.height
        clip: true
        focus: false

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) {
                currentIndex--
            }
            else {
                currentIndex = count - 1
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Beginning)
            }
            itemList.currentIndex = letterIndex[currentIndex].firstIndex
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Beginning)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) {
                currentIndex++
            }
            else {
                currentIndex = 0
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Beginning)
            }
            itemList.currentIndex = letterIndex[currentIndex].firstIndex
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Beginning)
        }
        Keys.onReturnPressed: {
            letterNavActive = false
            itemList.forceActiveFocus()
        }
        Keys.onLeftPressed: {
            letterNavActive = false
            itemList.forceActiveFocus()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                letterNavActive = false
                itemList.forceActiveFocus()
                event.accepted = true
            }
        }

        delegate: Item {
            width: letterList.width
            height: root.sh * 0.04375 //21

            Rectangle {
                color: root.accentColor
                anchors.fill: parent
                visible: letterList.currentIndex === index && letterNavActive
            }

            Text {
                text: modelData.letter
                color: (letterList.currentIndex === index && letterNavActive)
                       ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0354167 //17
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                rightPadding: root.sw * 0.009375 //6
                topPadding: root.sh * 0.0041667 //2
                bottomPadding: root.sh * 0.00625 //3
            }
        }
    }

    // Footer
    Text {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.browse + ":BROWSE " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
