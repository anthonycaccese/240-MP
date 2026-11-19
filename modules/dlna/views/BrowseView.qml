import QtQuick
import Components

FocusScope {
    id: view

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goUp()
            event.accepted = true
        }
    }

    // Header
    AppBar {
        id: header
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: view.serverLabel
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    property string errorMessage: ""

    // Set while a pinned shortcut waits for its server to answer discovery.
    property bool waiting: false

    // Opened from the server list, or straight from a pinned main-menu row.
    readonly property string serverLabel: moduleRoot.currentServer
        ? moduleRoot.currentServer.name
        : (navParams.serverName || "Browse")

    // Plain copies of the current listing. ListModel.get() hands back a
    // model-owned reference that dies with this view, so anything passed to
    // another view is read from here instead.
    property var rows: []

    // The trail and current position are kept on moduleRoot, because opening
    // DetailView unloads this view and it is rebuilt from its original params.
    // Seeded from navListState so returning from the detail view lands on the
    // row you left, per the navigateTo/goBack contract in CONTRIBUTING.md.
    property int restoreIndex: navListState.currentIndex !== undefined
                               ? navListState.currentIndex : -1

    // Copy-then-reassign rather than mutating in place: a QML var property
    // does not reliably persist an in-place push.
    function enterContainer(id, title) {
        var stack = moduleRoot.folderStack.slice()
        stack.push({ id: moduleRoot.currentContainerId,
                     title: moduleRoot.currentFolder,
                     index: itemList.currentIndex })
        moduleRoot.folderStack = stack

        moduleRoot.currentContainerId = id
        moduleRoot.currentFolder = title
        dlnaBackend.browseContainer(id)
    }

    function goUp() {
        if (moduleRoot.folderStack.length === 0) {
            view.goBack()      // at the top: leave the module view entirely
            return
        }
        var stack = moduleRoot.folderStack.slice()
        var prev = stack.pop()
        moduleRoot.folderStack = stack

        moduleRoot.currentContainerId = prev.id
        moduleRoot.currentFolder = prev.title
        restoreIndex = prev.index
        dlnaBackend.browseContainer(prev.id)
    }

    // Bumped on pinsChanged so the "* " markers in the list re-evaluate.
    property int pinRev: 0

    // Empty state — distinguishes "still waiting" from "it failed", so a
    // failed browse no longer looks like an endless load.
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333
        width: parent.width * 0.75
        visible: itemList.count === 0

        Text {
            text: errorMessage !== "" ? "Could not browse"
                  : waiting ? "Finding server..."
                  : "Loading items..."
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.05
        }

        Text {
            text: errorMessage
            visible: errorMessage !== ""
            color: root.tertiaryColor
            font.family: root.globalFont
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: root.sh * 0.0333333
        }
    }

    // Items list
    ListView {
        id: itemList
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        keyNavigationEnabled: true
        clip: true
        focus: true

        model: ListModel { id: browseModel }

        delegate: Item {
            width: itemList.width
            height: root.sh * 0.0583333

            Item {
                id: textClip
                width: Math.min(rowText.implicitWidth, itemList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: itemList.currentIndex === index
                }

                Text {
                    id: rowText
                    // Folders read as "NAME/", matching the local files list.
                    text: view.pinMark(model.id, model.isContainer)
                          + (model.isContainer ? (model.title + "/") : model.title)
                    color: itemList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    rightPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.05
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

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    itemList.currentIndex = index
                    view.openEntry(index)
                }
            }
        }

        // Controller/keyboard activation belongs on the ListView — delegates
        // never hold key focus.
        Keys.onReturnPressed: view.openEntry(itemList.currentIndex)

        Keys.onPressed: function(event) {
            // Right, not Space: matches the scripts and YouTube modules — a USB
            // remote reliably has a d-pad but often no space key. Only accepted
            // for Right, so the other handlers still see everything else.
            if (event.key === Qt.Key_Right) {
                view.togglePinForCurrent()
                event.accepted = true
            }
        }

        Keys.onUpPressed: {
            if (count === 0) return
            currentIndex = currentIndex > 0 ? currentIndex - 1 : count - 1
            itemList.positionViewAtIndex(currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            currentIndex = currentIndex < count - 1 ? currentIndex + 1 : 0
            itemList.positionViewAtIndex(currentIndex, ListView.Contain)
        }
    }

    // A server's top level is usually Browse Folders / Music / Pictures / Video.
    // The folder tree already contains all of it, so the category list costs a
    // step without adding reach — open it directly. Fires only on the first load
    // of this view, so browsing back to the root cannot bounce the user forward.
    // Returns true when it has taken over, meaning the caller must not render.
    function openFolderView(items) {
        if (moduleRoot.categorySkipped)
            return false
        moduleRoot.categorySkipped = true

        for (var i = 0; i < items.length; i++) {
            if (items[i].isContainer
                && String(items[i].title).toLowerCase() === "browse folders") {
                moduleRoot.currentContainerId = items[i].id
                moduleRoot.currentFolder = items[i].title
                dlnaBackend.browseContainer(items[i].id)
                return true
            }
        }
        return false   // server exposes no folder view — show what it does have
    }

    // Only folders can be pinned — a pinned row opens a browse location, and a
    // playable item would need a different route entirely.
    function togglePinForCurrent() {
        var entry = view.rows[itemList.currentIndex]
        if (!entry || !entry.isContainer)
            return
        dlnaBackend.togglePin(dlnaBackend.currentServerUdn(), view.serverLabel,
                              entry.id, entry.title)
    }

    // Reads pinRev so the binding re-runs when the pin set changes.
    function pinMark(objectId, isContainer) {
        if (!isContainer || pinRev < 0)
            return ""
        return dlnaBackend.isPinned(dlnaBackend.currentServerUdn(), objectId) ? "* " : ""
    }

    function openEntry(index) {
        var entry = view.rows[index]
        if (!entry)
            return
        if (entry.isContainer) {
            view.enterContainer(entry.id, entry.title)
        } else {
            view.navigateTo("DetailView.qml",
                            { item: entry, folder: moduleRoot.currentFolder },
                            { currentIndex: itemList.currentIndex })
        }
    }

    Connections {
        target: dlnaBackend

        function onPinsChanged() {
            view.pinRev++
        }

        function onPinnedResolveFailed(message) {
            view.waiting = false
            view.errorMessage = message
        }

        function onItemsLoaded() {
            errorMessage = ""
            waiting = false
            var items = dlnaBackend.getCurrentItems()

            // Decided before the model is touched: populating first and then
            // jumping would render the category list for a frame.
            if (openFolderView(items))
                return

            rows = items
            browseModel.clear()
            for (var i = 0; i < items.length; i++) {
                browseModel.append(items[i])
            }

            // Coming back up: land on the folder that was just left.
            if (restoreIndex >= 0) {
                itemList.currentIndex = Math.min(restoreIndex, browseModel.count - 1)
                itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
                restoreIndex = -1
            }
        }

        function onBrowseError(error) {
            errorMessage = error
            browseModel.clear()
        }
    }

    // Footer
    Text {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + root.hints.browse + ":PIN " + root.hints.select + ":SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }

    Component.onCompleted: {
        // Returning from DetailView rebuilds this view, so resume at the folder
        // that was on screen rather than starting from the root again.
        if (navParams.pinned && !moduleRoot.pinResolved) {
            moduleRoot.pinResolved = true
            waiting = true
            dlnaBackend.openPinned(navParams.serverUdn, navParams.objectId)
        } else {
            dlnaBackend.browseContainer(moduleRoot.currentContainerId)
        }
        itemList.forceActiveFocus()
    }
}
