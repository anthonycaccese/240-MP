import QtQuick
import Components

// Shown when the module can't start: no location file, an unusable one, or a
// name that doesn't resolve. Explains exactly what to do rather than failing
// silently on a black screen.
FocusScope {
    id: setupRoot

    property var navParams: ({})
    signal goBack()

    // "missing" | "empty" | "unreadable" | "notfound" | "network"
    property string reason: navParams.reason || "missing"
    property string locationPath: weatherBackend ? weatherBackend.location_file_path() : ""

    property string headline: {
        switch (reason) {
        case "notfound":   return "LOCATION NOT FOUND"
        case "network":    return "COULD NOT REACH THE LOCATION SERVICE"
        case "unreadable": return "LOCATION FILE COULD NOT BE READ"
        case "empty":      return "LOCATION FILE IS EMPTY"
        default:           return "NO LOCATION SET"
        }
    }

    property string detail: {
        switch (reason) {
        case "notfound":
            return "No place matched that name — try adding a country, e.g. Paris, France"
        case "network":
            return "Check the network connection and try again"
        case "unreadable":
            return "Check the file's permissions"
        default:
            return "Create this file and put your location in it:"
        }
    }

    property bool showPath: reason !== "network"

    focus: true

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    Column {
        anchors.centerIn: parent
        width: root.sw * 0.76875
        spacing: root.sh * 0.0333333

        Text {
            text: setupRoot.headline
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.05
        }

        Text {
            text: setupRoot.detail
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.0333333
        }

        Text {
            visible: setupRoot.showPath && setupRoot.locationPath !== ""
            text: setupRoot.locationPath
            color: root.primaryColor
            font.family: root.globalFont
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WrapAnywhere
            font.pixelSize: root.sh * 0.0291667
        }

        Text {
            visible: setupRoot.showPath
            text: "EXAMPLE:  CALDWELL, NJ, USA\n"
                  + "OR COORDINATES:  40.8398, -74.2765\n"
                  + "OR WITH A NAME:  40.8398, -74.2765, CALDWELL"
            color: root.tertiaryColor
            font.family: root.globalFont
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            font.pixelSize: root.sh * 0.0291667
        }
    }

    Text {
        text: root.hints.back + ":BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
