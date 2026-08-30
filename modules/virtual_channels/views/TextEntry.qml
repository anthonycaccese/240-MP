import QtQuick
import Components

FocusScope {
    id: entryRoot
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property var navListState: ({})
    property string moduleId:   navParams.moduleId   || ""
    property string settingKey: navParams.settingKey || ""
    property string title:      navParams.title      || "Enter Name"
    property int    maxLength:  navParams.maxLength  || 24

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string text: navParams.initialText || ""

    readonly property var keyRows: [
        ["Q","W","E","R","T","Y","U","I","O","P"],
        ["A","S","D","F","G","H","J","K","L","-"],
        ["Z","X","C","V","B","N","M","'",".","&"],
        ["1","2","3","4","5","6","7","8","9","0"],
        ["SPACE","DELETE","CLEAR","DONE"]
    ]

    readonly property int doneRow: keyRows.length - 1
    readonly property int doneCol: keyRows[keyRows.length - 1].indexOf("DONE")

    property int rowIndex: 0
    property int colIndex: 0

    focus: true

    function rowLength(r) { return keyRows[r].length }

    function commit() {
        if (text.trim() === "") return
        appCore.save_setting(moduleId, settingKey, text.trim())
        goBack()
    }

    function press(key) {
        switch (key) {
        case "SPACE":  if (text.length < maxLength) text += " "; break
        case "DELETE": text = text.slice(0, -1); break
        case "CLEAR":  text = ""; break
        case "DONE":   commit(); break
        default:       if (text.length < maxLength) text += key
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Backspace) {
            press("DELETE")
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Up) {
            rowIndex = (rowIndex - 1 + keyRows.length) % keyRows.length
            colIndex = Math.min(colIndex, rowLength(rowIndex) - 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            rowIndex = (rowIndex + 1) % keyRows.length
            colIndex = Math.min(colIndex, rowLength(rowIndex) - 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            colIndex = (colIndex - 1 + rowLength(rowIndex)) % rowLength(rowIndex)
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            colIndex = (colIndex + 1) % rowLength(rowIndex)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            press(keyRows[rowIndex][colIndex])
            event.accepted = true
        } else if (event.text.length === 1) {
            var ch = event.text.toUpperCase()
            if (/[A-Z0-9 \-&'.]/.test(ch) && text.length < maxLength) {
                text += ch
                rowIndex = doneRow
                colIndex = doneCol
                event.accepted = true
            }
        }
    }

    AppBar {
        id: appBar
        iconSource: entryRoot.moduleIcon
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.075
        anchors.leftMargin: root.sw * 0.125
        title: entryRoot.title
    }

    Rectangle {
        id: field
        anchors.top: appBar.bottom
        anchors.topMargin: root.sh * 0.02
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.70
        height: root.sh * 0.07
        color: root.surfaceColor
        border.color: root.tertiaryColor
        border.width: root.sh * 0.003125

        Text {
            id: typed
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: root.sw * 0.0125
            text: entryRoot.text
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.042
        }

        Rectangle {
            width: root.sw * 0.004
            height: parent.height * 0.55
            color: root.primaryColor
            anchors.verticalCenter: parent.verticalCenter
            x: typed.x + typed.implicitWidth + root.sw * 0.004
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { to: 0; duration: 500 }
                NumberAnimation { to: 1; duration: 500 }
            }
        }
    }

    Column {
        id: keyboard
        anchors.top: field.bottom
        anchors.topMargin: root.sh * 0.022
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.sh * 0.007

        Repeater {
            id: rowRepeater
            model: entryRoot.keyRows.length

            delegate: Row {
                id: keyRow
                required property int index
                spacing: root.sw * 0.007
                anchors.horizontalCenter: parent.horizontalCenter

                Repeater {
                    model: entryRoot.keyRows[keyRow.index].length

                    delegate: Rectangle {
                        id: keyCell
                        required property int index
                        readonly property string keyText: entryRoot.keyRows[keyRow.index][index]
                        readonly property bool isWord: keyText.length > 1
                        readonly property bool selected: keyRow.index === entryRoot.rowIndex
                                                         && index === entryRoot.colIndex

                        width: isWord ? root.sw * 0.148 : root.sw * 0.058
                        height: root.sh * 0.060
                        color: selected ? root.accentColor : root.surfaceColor
                        border.color: selected ? root.accentColor : root.tertiaryColor
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: keyCell.keyText
                            color: keyCell.selected ? root.surfaceColor : root.primaryColor
                            font.family: root.globalFont
                            font.pixelSize: keyCell.isWord ? root.sh * 0.025
                                                           : root.sh * 0.034
                        }
                    }
                }
            }
        }
    }

    Text {
        anchors.top: keyboard.bottom
        anchors.topMargin: root.sh * 0.018
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.sw * 0.75
        horizontalAlignment: Text.AlignHCenter
        text: "TYPE ON A KEYBOARD OR PICK LETTERS — THEN PRESS ENTER ON DONE"
        color: root.tertiaryColor
        font.family: root.globalFont
        wrapMode: Text.WordWrap
        font.pixelSize: root.sh * 0.0270833
    }

    Text {
        text: root.hints.back + ":CANCEL "
              + root.hints.navigate + root.hints.change + ":MOVE "
              + root.hints.select + ":PRESS"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.0833333
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
    }
}
