import QtQuick
import Components

// Playlist setup: an M3U URL (or local file path) plus an optional XMLTV EPG
// URL for the now/next guide. Saved to modules.<id>.m3u_url / .epg_url.
FocusScope {
    focus: true
    id: setupRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal replaceWith(string path, var params)
    signal goBack()

    property string m3uUrl: appCore.get_setting(moduleRoot.moduleId, "m3u_url") || ""
    property string epgUrl: appCore.get_setting(moduleRoot.moduleId, "epg_url") || ""
    property bool waiting: false
    property string errorMsg: ""

    // Focus index: 0=M3U URL, 1=EPG URL, 2=Load
    property int focusIndex: 0

    Connections {
        target: iptvBackend

        function onGroupsLoaded(groups) {
            setupRoot.waiting = false
            appCore.save_setting(moduleRoot.moduleId, "m3u_url", setupRoot.m3uUrl)
            appCore.save_setting(moduleRoot.moduleId, "epg_url", setupRoot.epgUrl)
            setupRoot.replaceWith("Groups.qml", {})
        }

        function onPlaylistError(msg) {
            setupRoot.waiting = false
            setupRoot.errorMsg = msg
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Shift || event.key === Qt.Key_Control ||
            event.key === Qt.Key_Alt   || event.key === Qt.Key_Meta ||
            event.key === Qt.Key_AltGr) {
            return
        }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
            return
        }
        if (waiting) {
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Up) {
            if (focusIndex > 0) focusIndex--
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            if (focusIndex < 2) focusIndex++
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            setupRoot.submit()
            event.accepted = true
        }
    }

    function submit() {
        if (waiting) return
        if (m3uUrl === "") {
            errorMsg = "Enter an M3U playlist URL or file path"
            return
        }
        waiting = true
        errorMsg = ""
        // Persist first so the backend reads the new source, then load.
        appCore.save_setting(moduleRoot.moduleId, "m3u_url", m3uUrl)
        appCore.save_setting(moduleRoot.moduleId, "epg_url", epgUrl)
        iptvBackend.load_groups(true)
    }

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Add a Playlist"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    // Body
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333

        // Reusable labelled field (keys forwarded up to setupRoot.Keys).
        component Field: Column {
            property alias label: fieldLabel.text
            property alias text: fieldInput.text
            property int index: 0
            signal edited(string value)

            spacing: root.sh * 0.0166667
            width: root.sw * 0.6
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                id: fieldLabel
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0291667
            }

            Rectangle {
                width: parent.width
                height: root.sh * 0.075
                color: root.surfaceColor
                border.color: setupRoot.focusIndex === index ? root.accentColor : root.tertiaryColor
                border.width: root.sh * 0.003125

                TextInput {
                    id: fieldInput
                    anchors.fill: parent
                    anchors.margins: root.sh * 0.0166667
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0375
                    clip: true
                    focus: setupRoot.focusIndex === index

                    onTextChanged: edited(text)
                    Keys.onPressed: function(event) { event.accepted = false }
                }
            }
        }

        Field {
            label: "M3U Playlist URL"
            index: 0
            text: setupRoot.m3uUrl
            onEdited: function(value) { setupRoot.m3uUrl = value }
        }

        Field {
            label: "XMLTV Guide URL (optional)"
            index: 1
            text: setupRoot.epgUrl
            onEdited: function(value) { setupRoot.epgUrl = value }
        }

        // Load button
        Rectangle {
            width: root.sw * 0.234375
            height: root.sh * 0.0583333
            color: focusIndex === 2 ? root.accentColor : root.surfaceColor
            border.color: focusIndex === 2 ? root.accentColor : root.tertiaryColor
            border.width: root.sh * 0.003125
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                anchors.centerIn: parent
                text: "Load"
                color: focusIndex === 2 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0375
            }
        }

        Text {
            visible: waiting
            text: "LOADING PLAYLIST..."
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333
        }

        Text {
            visible: errorMsg !== ""
            text: errorMsg
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.sw * 0.6
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.0333333
        }
    }

    // Footer
    Text {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":LOAD"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
