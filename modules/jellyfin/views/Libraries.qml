import QtQuick
import Components

// Main Jellyfin home screen: Continue Watching + library list
FocusScope {
    id: browseRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var libraries: []
    property string serverName: ""
    property string userName: ""

    Connections {
        target: jellyfinBackend

        function onLibrariesLoaded(items) {
            // The backend prepends Continue Watching / Up Next (only when they
            // have content), so render the list as given.
            browseRoot.libraries = items
            if (items.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                libraryList.currentIndex = Math.min(restore, items.length - 1)
                libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            console.log("[Jellyfin Library] Error: " + msg)
        }
    }

    Component.onCompleted: {
        browseRoot.serverName = jellyfinBackend.get_server_name()
        browseRoot.userName = jellyfinBackend.get_user_name()
        jellyfinBackend.load_libraries()
    }

    focus: true

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: browseRoot.serverName + (browseRoot.userName ? " (" + browseRoot.userName + ")" : "")
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Loading Indicator
    Text {
        visible: libraries.length === 0
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    ListView {
        id: libraryList
        model: libraries
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            var lib = libraries[currentIndex]
            if (!lib) return

            if (lib.key === "continue_watching") {
                browseRoot.navigateTo("Items.qml", {
                    mode: "resume",
                    title: "CONTINUE WATCHING",
                    libraryName: "CONTINUE WATCHING"
                }, { currentIndex: libraryList.currentIndex })
                return
            }

            if (lib.key === "up_next") {
                browseRoot.navigateTo("Items.qml", {
                    mode: "up_next",
                    title: "NEXT UP",
                    libraryName: "NEXT UP"
                }, { currentIndex: libraryList.currentIndex })
                return
            }

            // Box-set libraries list their box sets (direct children); Items.qml
            // "boxset" mode fetches them and routes each box set into Boxset.qml.
            var collectionType = lib.collectionType || ""
            if (collectionType === "boxsets") {
                browseRoot.navigateTo("Items.qml", {
                    parentId: lib.itemId,
                    title: lib.title,
                    libraryName: lib.title,
                    mode: "boxset"
                }, { currentIndex: libraryList.currentIndex })
                return
            }

            // homevideos libraries are a tree of folders + videos, browsed one
            // level at a time via Items.qml "folder" mode.
            if (collectionType === "homevideos") {
                browseRoot.navigateTo("Items.qml", {
                    parentId: lib.itemId,
                    title: lib.title,
                    libraryName: lib.title,
                    mode: "folder"
                }, { currentIndex: libraryList.currentIndex })
                return
            }

            // Map the (gated) library type to its item-type filter.
            var includeTypes = ""
            if (collectionType === "movies") includeTypes = "Movie"
            else if (collectionType === "tvshows") includeTypes = "Series"
            browseRoot.navigateTo("Items.qml", {
                parentId: lib.itemId,
                title: lib.title,
                libraryName: lib.title,
                includeTypes: includeTypes
            }, { currentIndex: libraryList.currentIndex })
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                browseRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: libraryList.width
            height: root.sh * 0.0583333 //28

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, libraryList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: libraryList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.title || ""
                    color: libraryList.currentIndex === index ? root.surfaceColor : root.primaryColor
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
                    running: (libraryList.currentIndex === index) &&
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
