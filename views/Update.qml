import QtQuick
import Components

// Software Update page — checks GitHub Releases via the UpdateManager backend
// (context property "updateManager"), downloads the platform asset, and applies
// it. All state lives in C++; this view renders updateManager.state:
//   idle | checking | upToDate | updateAvailable | downloading | readyToApply | error
FocusScope {
    id: updateRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})

    property bool autostartSession: false
    property bool confirmOverlayVisible: false
    property int confirmChoiceIndex: 0

    // Null guards: context properties resolve to null while the view Loader
    // tears down; guarded bindings stay teardown-safe (see Main.qml mirrors).
    readonly property string updState: updateManager ? updateManager.state : "idle"
    readonly property string latestVersion: updateManager ? updateManager.latestVersion : ""
    readonly property string releaseNotes: updateManager ? updateManager.releaseNotes : ""
    readonly property string errorMessage: updateManager ? updateManager.errorMessage : ""
    readonly property string applyHint: updateManager ? updateManager.applyHint : ""
    readonly property bool canApply: updateManager ? updateManager.canApply : false
    readonly property bool devBuild: root.appVersion === "dev"

    // Dev builds can check (to read the notes) but not download; on the Pi a
    // download is pointless unless the launcher can actually apply it. macOS
    // always allows download — the no-canApply fallback opens the DMG manually.
    readonly property bool canDownload: !devBuild && (Qt.platform.os === "osx" || canApply)

    readonly property string actionLabel: {
        switch (updState) {
        case "idle": case "upToDate": return "CHECK"
        case "error":                 return "RETRY"
        case "updateAvailable":       return canDownload ? "DOWNLOAD" : ""
        case "readyToApply":          return "INSTALL"
        default:                      return ""
        }
    }

    readonly property string statusText: {
        switch (updState) {
        case "checking":        return "Checking for updates..."
        case "upToDate":        return "You're up to date"
        case "updateAvailable": return "Update available"
        case "downloading":     return "Downloading... " + Math.round((updateManager ? updateManager.downloadProgress : 0) * 100) + "%"
        case "readyToApply":    return latestVersion + " ready to install"
        case "error":           return errorMessage
        default:                return "Press " + root.hints.select + " to check for latest"
        }
    }

    // Contextual hint under the status line, when there is something to explain
    readonly property string hintText: {
        if (devBuild && (updState === "updateAvailable" || updState === "readyToApply"))
            return "Dev build — self-update is disabled."
        if (!canApply && applyHint && (updState === "updateAvailable" || updState === "readyToApply"))
            return applyHint
        return ""
    }

    readonly property string applyActionLabel: {
        if (Qt.platform.os === "osx")
            return canApply ? "Apply & Relaunch" : "Quit & Open Disk Image"
        return updateRoot.autostartSession ? "Apply & Restart" : "Quit & Apply on Next Launch"
    }
    // No "Cancel": Back/Escape dismisses the overlay instead. A downloaded
    // update stays inert until Apply is chosen (that's when the launcher-facing
    // marker is written), so backing out truly means "not now".
    readonly property var confirmOptions: [
        { label: updateRoot.applyActionLabel, action: "apply" },
        { label: "Discard Update",            action: "discard" }
    ]

    function primaryAction() {
        if (!updateManager) return
        switch (updState) {
        case "idle": case "upToDate": case "error":
            updateManager.checkForUpdates()
            break
        case "updateAvailable":
            if (canDownload) updateManager.download()
            break
        case "readyToApply":
            confirmChoiceIndex = 0
            confirmOverlayVisible = true
            break
        }
    }

    Component.onCompleted: autostartSession = appCore.isAutostartSession()

    // Header
    AppBar {
        iconSource: "../../assets/images/update.svg"
        title: "Update"
        //subtitle: root.appVersion
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Item {
        id: content
        focus: true
        anchors.fill: parent

        Keys.onReturnPressed: updateRoot.primaryAction()
        Keys.onUpPressed:   notesFlick.scrollBy(-1)
        Keys.onDownPressed: notesFlick.scrollBy(1)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                if (updateRoot.updState === "downloading" && updateManager)
                    updateManager.cancelDownload()
                updateRoot.goBack()
                event.accepted = true
            }
        }

        Column {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: root.sh * 0.25 //120
            anchors.leftMargin: root.sw * 0.125 //80
            width: root.sw * 0.76875 //492
            spacing: root.sh * 0.0291667 //14

            // Version summary
            Row {
                spacing: root.sw * 0.025 //16
                Text {
                    text: "INSTALLED: " + root.appVersion
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0375 //18
                }
                Text {
                    visible: updateRoot.latestVersion !== ""
                    text: "LATEST: " + updateRoot.latestVersion
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0375 //18
                }
            }

            // Status line
            Text {
                width: parent.width
                text: updateRoot.statusText
                color: root.accentColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                wrapMode: Text.WordWrap
                font.pixelSize: root.sh * 0.05 //24
            }

            // Contextual hint (dev build / old launcher / non-standard install)
            Text {
                visible: updateRoot.hintText !== ""
                width: parent.width
                text: updateRoot.hintText
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                font.pixelSize: root.sh * 0.0291667 //14
            }

            // Download progress bar
            Rectangle {
                visible: updateRoot.updState === "downloading"
                width: parent.width
                height: root.sh * 0.0291667 //14
                color: "transparent"
                border.color: root.tertiaryColor
                border.width: 1
                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.margins: 2
                    width: (parent.width - 4) * (updateManager ? updateManager.downloadProgress : 0)
                    color: root.accentColor
                }
            }

            // Release notes
            Rectangle {
                visible: updateRoot.releaseNotes !== ""
                width: parent.width
                height: root.sh * 0.3291667 //158
                property color baseColor: root.primaryColor
                color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.1)

                Flickable {
                    id: notesFlick
                    anchors.fill: parent
                    anchors.margins: root.sw * 0.009375 //6
                    contentHeight: notesText.height
                    clip: true

                    function scrollBy(direction) {
                        var step = height * 0.8
                        var maxY = Math.max(0, contentHeight - height)
                        contentY = Math.max(0, Math.min(maxY, contentY + direction * step))
                    }
                    Text {
                        id: notesText
                        width: notesFlick.width
                        text: updateRoot.releaseNotes
                        textFormat: Text.PlainText
                        color: root.primaryColor
                        font.family: root.globalFont
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.pixelSize: root.sh * 0.0291667 //14
                    }
                }
            }
        }

        // Footer
        Text {
            text: root.hints.back + ":BACK "
                  + (updateRoot.releaseNotes !== "" ? root.hints.navigate + ":SCROLL " : "")
                  + (updateRoot.actionLabel !== "" ? root.hints.select + ":" + updateRoot.actionLabel : "")
            color: root.tertiaryColor
            font.family: root.globalFont
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.bottomMargin: root.sh * 0.1041667 //50
            anchors.leftMargin: root.sw * 0.125 //80
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    // --- INSTALL CONFIRMATION OVERLAY --- (mirrors the Settings quit overlay)
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: confirmOverlayVisible
        focus: confirmOverlayVisible

        Keys.onUpPressed:   { confirmChoiceIndex = Math.max(0, confirmChoiceIndex - 1) }
        Keys.onDownPressed: { confirmChoiceIndex = Math.min(confirmOptions.length - 1, confirmChoiceIndex + 1) }
        Keys.onReturnPressed: {
            var act = confirmOptions[confirmChoiceIndex].action
            confirmOverlayVisible = false
            if (act === "apply" && updateManager) {
                // Linux exits with code 11 under autostart (see 240mp-stop in
                // scripts/install.sh) or quits for apply-on-next-launch; macOS
                // spawns the swap helper and quits.
                updateManager.applyAndRestart()
            } else if (act === "discard" && updateManager) {
                updateManager.discardStagedUpdate()
                content.forceActiveFocus()
            } else {
                content.forceActiveFocus()
            }
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                confirmOverlayVisible = false
                content.forceActiveFocus()
                event.accepted = true
            }
        }

        Rectangle {
            color: root.surfaceColor
            anchors.centerIn: parent
            width: root.sw * 0.76875   //492
            height: root.sh * 0.2833333 //136

            Column {
                id: confirmDialogColumn
                anchors.fill: parent
                spacing: root.sh * 0.05 //24

                Text {
                    text: "INSTALL " + updateRoot.latestVersion + "?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: confirmOptions
                        delegate: Item {
                            width: confirmDialogColumn.width
                            height: root.sh * 0.0583333 //28

                            Rectangle {
                                anchors.fill: confirmOptionText
                                color: root.accentColor
                                visible: index === confirmChoiceIndex
                            }

                            Text {
                                id: confirmOptionText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: index === confirmChoiceIndex ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.capitalization: Font.AllUppercase
                                topPadding: root.sh * 0.0041667 //2
                                leftPadding: root.sw * 0.009375 //6
                                rightPadding: root.sw * 0.009375 //6
                                bottomPadding: root.sh * 0.00625 //3
                                font.pixelSize: root.sh * 0.05 //24
                            }
                        }
                    }
                }

                Text {
                    text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }
}
