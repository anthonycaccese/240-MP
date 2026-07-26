import QtQuick

// Three-day forecast: centred title, then one column per day.
//
// Condition text wraps to a second line on its own (SHOWERS / T'STORMS in the
// original), which is why each column is a fixed-width Column rather than a row
// of single labels — the LO/HI block has to stay aligned across columns even
// when one condition wraps and the others don't.
Item {
    id: screen

    property var days: []

    readonly property real lineSize: root.sh * 0.075
    readonly property real colWidth: width / 3

    Text {
        id: title
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        text: "EXTENDED FORECAST"
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: screen.lineSize
    }

    Row {
        id: columns
        anchors.top: title.bottom
        anchors.topMargin: root.sh * 0.075
        anchors.left: parent.left
        anchors.right: parent.right

        Repeater {
            model: screen.days

            Item {
                width: screen.colWidth
                height: columns.height

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: root.sw * 0.02
                    spacing: root.sh * 0.012

                    Text {
                        text: modelData.name || ""
                        color: root.primaryColor
                        font.family: root.globalFont
                        font.pixelSize: screen.lineSize
                    }
                    Text {
                        text: modelData.condition || ""
                        color: root.primaryColor
                        font.family: root.globalFont
                        font.pixelSize: screen.lineSize
                        width: parent.width
                        wrapMode: Text.WordWrap
                    }
                }

                // Anchored to the bottom of the column rather than flowing after
                // the condition, so LO/HI line up across all three days
                // regardless of how many lines the condition took.
                Column {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    spacing: root.sh * 0.012

                    Text {
                        text: "LO: " + (modelData.lo || "")
                        color: root.primaryColor
                        font.family: root.globalFont
                        font.pixelSize: screen.lineSize
                    }
                    Text {
                        text: "HI: " + (modelData.hi || "")
                        color: root.primaryColor
                        font.family: root.globalFont
                        font.pixelSize: screen.lineSize
                    }
                }
            }
        }
    }

    // Row has no implicit height from anchors alone; give the columns the space
    // between the title and the bottom of the screen area.
    Binding {
        target: columns
        property: "height"
        value: screen.height - title.height - (root.sh * 0.075)
    }
}
