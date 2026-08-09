import QtQuick
import Components

// Console run mode: 240-MP keeps the screen and shows the script's output.
// Identical on every target — nothing is handed off.
FocusScope {
    id: consoleRoot

    property var navParams: ({})
    property string basename: navParams.basename || ""
    property string scriptName: navParams.name || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    // "" while running, then the finish reason. Kept separate from the backend's
    // running flag so the view can hold the result on screen after exit.
    property string finishReason: ""
    property int    finishExitCode: 0
    property string startError: ""

    readonly property bool running: scriptsBackend.scriptRunning
    readonly property bool done: finishReason !== "" || startError !== ""

    focus: true

    Component.onCompleted: {
        outputFlick.forceActiveFocus()
        if (!scriptsBackend.runScript(basename))
            startError = scriptsBackend.lastError()
    }

    Connections {
        target: scriptsBackend
        function onScriptFinished(exitCode, reason) {
            consoleRoot.finishExitCode = exitCode
            consoleRoot.finishReason = reason
        }
        function onConsoleOutputChanged() {
            // Follow the tail only while the user hasn't scrolled up.
            if (outputFlick.followTail)
                Qt.callLater(outputFlick.scrollToEnd)
        }
    }

    Keys.onPressed: function(event) {
        // Ignore auto-repeat: holding Back should behave exactly like one press.
        // Without this, a held key repeats this handler ~10x/second — which used to
        // keep re-arming the stop grace so a stubborn script never got killed, and
        // once it had stopped would walk the user back out through several views.
        if (event.isAutoRepeat) { event.accepted = true; return }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            // While running, ESC stops the script; the view stays so the tail and
            // exit status remain readable. A second ESC leaves.
            if (running) scriptsBackend.stopScript()
            else         goBack()
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
        subtitle: consoleRoot.scriptName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Status line
    Text {
        id: statusText
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.2166667 //104
        anchors.leftMargin: root.sw * 0.125 //80
        text: {
            if (consoleRoot.startError !== "") return "COULD NOT START: " + consoleRoot.startError
            if (consoleRoot.running)           return "RUNNING…"
            if (consoleRoot.finishReason === "stopped")         return "STOPPED"
            if (consoleRoot.finishReason === "failed_to_start") return "FAILED TO START"
            if (consoleRoot.finishReason !== "")                return "EXIT " + consoleRoot.finishExitCode
            return ""
        }
        color: {
            if (consoleRoot.startError !== "") return root.accentColor
            if (consoleRoot.running)           return root.secondaryColor
            if (consoleRoot.finishReason === "ok") return root.secondaryColor
            if (consoleRoot.finishReason === "")   return root.secondaryColor
            return root.accentColor
        }
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // Output
    Rectangle {
        anchors.top: statusText.bottom
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.0208333 //10
        anchors.leftMargin: root.sw * 0.125 //80
        width: root.sw * 0.75 //480
        height: root.sh * 0.475 //228
        // A script that never started has no output of its own; the buffer still
        // holds the LAST run's, and showing that under a "could not start" error
        // would be actively misleading.
        visible: consoleRoot.startError === ""
        property color baseColor: root.primaryColor
        color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.1)

        Flickable {
            id: outputFlick
            anchors.fill: parent
            anchors.margins: root.sw * 0.009375 //6
            contentHeight: outputText.height
            clip: true
            focus: true

            // True while the view is pinned to the bottom. Recomputed from the
            // user's own scrolling, so following the tail stops the moment they
            // scroll up and resumes when they come back down.
            property bool followTail: true

            function scrollToEnd() {
                var maxY = Math.max(0, contentHeight - height)
                contentY = maxY
            }
            function scrollBy(direction) {
                var step = height * 0.5
                var maxY = Math.max(0, contentHeight - height)
                contentY = Math.max(0, Math.min(maxY, contentY + direction * step))
                followTail = (contentY >= maxY - 1)
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

    // Footer
    Text {
        text: (consoleRoot.running ? root.hints.back + ":STOP " : root.hints.back + ":BACK ")
              + root.hints.navigate + ":SCROLL"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }
}
