import QtQuick
import Components

// Playlist setup: enter an M3U URL (or a local file path). Saved to
// modules.<id>.m3u_url; the backend fetches + parses it on load.
FocusScope {
    focus: true
    id: setupRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal replaceWith(string path, var params)
    signal goBack()

    property string m3uUrl: appCore.get_setting(moduleRoot.moduleId, "m3u_url") || ""
    property bool waiting: false
    property string errorMsg: ""

    Connections {
        target: iptvBackend

        function onGroupsLoaded(groups) {
            // Playlist parsed successfully — remember it and enter the guide.
            setupRoot.waiting = false
            appCore.save_setting(moduleRoot.moduleId, "m3u_url", setupRoot.m3uUrl)
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
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
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

        Column {
            spacing: root.sh * 0.0166667
            width: root.sw * 0.6
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                text: "M3U Playlist URL"
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0291667
            }

            Rectangle {
                width: parent.width
                height: root.sh * 0.075
                color: root.surfaceColor
                border.color: root.accentColor
                border.width: root.sh * 0.003125

                TextInput {
                    id: urlInput
                    anchors.fill: parent
                    anchors.margins: root.sh * 0.0166667
                    text: setupRoot.m3uUrl
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0375
                    clip: true
                    focus: true

                    onTextChanged: { setupRoot.m3uUrl = text }

                    Keys.onPressed: function(event) {
                        event.accepted = false
                    }
                }
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
        text: root.hints.back + ":BACK " + root.hints.select + ":LOAD"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
