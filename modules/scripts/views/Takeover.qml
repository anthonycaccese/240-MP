import QtQuick
import Components

// Takeover run mode: the script owns the screen.
//
// On a headless Pi this view is not visible while the script runs — Qt has been
// VT-switched away and its rendering is suspended. It exists to say what is
// happening before the hand-off and, more importantly, to report a failure
// afterwards, since that is the only place the user can see one.
//
// On macOS / desktop Linux / SteamOS nothing is handed off: the child's window
// simply covers ours, so this view sits behind it.
FocusScope {
    id: takeoverRoot

    property var navParams: ({})
    property string basename: navParams.basename || ""
    property string scriptName: navParams.name || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string finishReason: ""
    property int    finishExitCode: 0
    property string startError: ""
    // Set when the launcher refused the hand-off (the display state could not be
    // saved, so it could not have been restored) and ran the script anyway.
    property bool   downgraded: false

    // "busy" rather than "running": after the script exits we may still be waiting
    // on the processes it left behind before the display is ours again, and the
    // user must not be able to leave this view during that window.
    readonly property bool busy: scriptsBackend.scriptBusy
    readonly property bool done: finishReason !== "" || startError !== ""

    focus: true

    // On a headless Pi the VT switch inside runScript() suspends Qt's renderer,
    // so whatever is on screen at that moment will stay there for the whole run.
    // So: paint the hand-off screen, let it reach the display, and only then
    // launch. A timer rather than Qt.callLater because what matters is a frame
    // actually PRESENTED, not just the scene graph being updated; ~120 ms is
    // seven frames at 60 Hz and it feels invisible next to a process launch.
    Component.onCompleted: launchTimer.start()

    Timer {
        id: launchTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (!scriptsBackend.runScript(takeoverRoot.basename))
                takeoverRoot.startError = scriptsBackend.lastError()
            else
                takeoverRoot.downgraded = scriptsBackend.wasDowngraded()
        }
    }

    Connections {
        target: scriptsBackend
        function onScriptFinished(exitCode, reason) {
            takeoverRoot.finishExitCode = exitCode
            takeoverRoot.finishReason = reason
            // A clean run returns to the list the way playback does. Anything else
            // holds the view so the reason and any captured output stay readable —
            // on a Pi this is the user's only window into what went wrong.
            if (reason === "ok")
                takeoverRoot.goBack()
        }
    }

    Keys.onPressed: function(event) {
        // Ignore auto-repeat: holding Back should behave exactly like one press.
        // Without this, a held key repeats this handler ~10x/second — which used to
        // keep re-arming the stop grace so a stubborn script never got killed, and
        // once it had stopped would walk the user back out through several views.
        if (event.isAutoRepeat) { event.accepted = true; return }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            // Also stops during the drain: leaving then would pop this view while a
            // headless Pi's framebuffer still belongs to the script's children.
            if (busy) scriptsBackend.stopScript()
            else      goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            outputFlick.scrollBy(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            outputFlick.scrollBy(1)
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: takeoverRoot.scriptName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Text {
        id: statusText
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.2166667 //104
        anchors.leftMargin: root.sw * 0.125 //80
        text: {
            if (takeoverRoot.startError !== "") return "COULD NOT START: " + takeoverRoot.startError
            if (scriptsBackend.scriptDraining)  return "WAITING FOR PROCESSES THIS SCRIPT LEFT RUNNING…"
            if (scriptsBackend.scriptRunning)   return "RUNNING — THIS SCRIPT HAS THE SCREEN"
            if (takeoverRoot.finishReason === "stopped")         return "STOPPED"
            if (takeoverRoot.finishReason === "failed_to_start") return "FAILED TO START"
            if (takeoverRoot.finishReason !== "")                return "EXIT " + takeoverRoot.finishExitCode
            return "HANDING OFF…"
        }
        color: (takeoverRoot.startError !== "" || takeoverRoot.finishReason === "failed"
                || takeoverRoot.finishReason === "failed_to_start")
               ? root.accentColor : root.secondaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // Warn when the sidecar asked for takeover but we ran in console mode instead.
    Text {
        id: downgradeNote
        visible: takeoverRoot.downgraded
        anchors.top: statusText.bottom
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.0104167 //5
        anchors.leftMargin: root.sw * 0.125 //80
        text: "DISPLAY COULD NOT BE HANDED OVER — RAN WITHOUT IT"
        color: root.accentColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0291667 //14
    }

    // Output is captured unless the script was given a real terminal, in which
    // case it went to that terminal instead and this stays empty.
    Rectangle {
        anchors.top: takeoverRoot.downgraded ? downgradeNote.bottom : statusText.bottom
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.0208333 //10
        anchors.leftMargin: root.sw * 0.125 //80
        width: root.sw * 0.75 //480
        height: root.sh * 0.45 //216
        // As in Console.qml: a script that never started has no output of its own,
        // so don't show the previous run's under a "could not start" error.
        visible: takeoverRoot.startError === "" && scriptsBackend.consoleOutput !== ""
        property color baseColor: root.primaryColor
        color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.1)

        Flickable {
            id: outputFlick
            anchors.fill: parent
            anchors.margins: root.sw * 0.009375 //6
            contentHeight: outputText.height
            clip: true

            function scrollBy(direction) {
                var step = height * 0.5
                var maxY = Math.max(0, contentHeight - height)
                contentY = Math.max(0, Math.min(maxY, contentY + direction * step))
            }

            Text {
                id: outputText
                width: outputFlick.width
                text: scriptsBackend.consoleOutput
                textFormat: Text.PlainText
                color: root.primaryColor
                font.family: root.globalFont
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                font.pixelSize: root.sh * 0.0291667 //14
            }
        }
    }

    Text {
        text: (takeoverRoot.busy ? root.hints.back + ":STOP" : root.hints.back + ":BACK")
              + (scriptsBackend.consoleOutput !== "" ? " " + root.hints.navigate + ":SCROLL" : "")
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // The hand-off screen. Opaque black over everything above, because on a
    // headless Pi this is the frame that FREEZES on the display for as long as
    // the script runs (see Component.onCompleted). It hides again the moment 
    // the run ends
    Rectangle {
        anchors.fill: parent
        z: 200
        visible: !takeoverRoot.done
        color: "black"

        Column {
            anchors.centerIn: parent
            width: parent.width * 0.75
            spacing: root.sh * 0.0333333 //16

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "Running..."
                color: "#919191"
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0333333 //16
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: takeoverRoot.scriptName
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0416667 //20
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: root.hints.back + ":STOP"
                color: "#919191"
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667 //14
            }
        }
    }
}
