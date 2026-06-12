import QtQuick
import Components

FocusScope {
    id: userSelectRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()

    property var users: navParams.users || []

    Connections {
        target: plexBackend

        function onUsersLoaded(loadedUsers) {
            userSelectRoot.users = loadedUsers
        }

        function onServersLoaded(servers) {
            if (!navParams.reauth) {
                userSelectRoot.navigateTo("ServerSelect.qml", { servers: servers })
            }
        }

        function onAuthSuccess() {
            if (navParams.reauth) {
                userSelectRoot.navigateTo("Libraries.qml", {})
            }
        }

        function onErrorOccurred(msg) {
            console.log("[UserSelect] Error: " + msg)
        }
    }

    Component.onCompleted: {
        // If users weren't passed via navParams, load from cache
        if (!navParams.users || navParams.users.length === 0) {
            plexBackend.load_users_from_cache()
        }
        if (users.length > 0) userList.currentIndex = 0
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

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Select User"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Body
    ListView {
        id: userList
        model: users
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: if (currentIndex > 0) currentIndex--
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++

        Keys.onReturnPressed: {
            var user = users[currentIndex]
            if (user) {
                if (navParams.reauth) {
                    plexBackend.reauth_select_user(user.id)
                } else {
                    plexBackend.select_user(user.id)
                }
            }
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                userSelectRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: userList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, userList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: userList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.title || ""
                    color: userList.currentIndex === index ? root.surfaceColor : root.primaryColor
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
}
