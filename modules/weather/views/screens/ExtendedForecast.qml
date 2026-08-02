import QtQuick
import QtQuick.Effects

// Three-day forecast: centred title, then one column per day.
//
// Condition text wraps to a second line on its own (SHOWERS / T'STORMS in the
// original), which is why each column is a fixed-width Column rather than a row
// of single labels — the LO/HI block has to stay aligned across columns even
// when one condition wraps and the others don't. The icon is anchored the same
// way and for the same reason.
Item {
    id: screen

    property var days: []

    readonly property real lineSize: root.sh * 0.05 //24
    readonly property real colWidth: width / 3
    readonly property real iconSize: root.sh * 0.1125 //54

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
        anchors.topMargin: root.sh * 0.0854167 //41
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
                    spacing: root.sh * 0.0138021 //6.625

                    Text {
                        text: modelData.name || ""
                        color: root.primaryColor
                        font.family: root.globalFont
                        font.pixelSize: screen.lineSize
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Rectangle {
                        height: screen.lineSize
                        width: parent.width
                        color: "transparent"
                        Text {
                            text: modelData.condition || ""
                            color: root.secondaryColor
                            font.family: root.globalFont
                            font.pixelSize: root.sh * 0.0375 //18
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            anchors.verticalCenter: parent.verticalCenter
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Anchored above LO/HI rather than centred in the gap below the
                // condition: PARTLY CLOUDY wraps to two lines where RAIN does
                // not, so that gap differs per column and centring in it would
                // leave the three icons at different heights.
                Item {
                    id: dayIcon
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: temps.top
                    anchors.bottomMargin: root.sh * 0.045
                    width:  dayIconImage.width
                    height: screen.iconSize
                    visible: dayIconImage.source != ""

                    Image {
                        id: dayIconImage
                        visible: false
                        height: parent.height
                        sourceSize.height: height
                        fillMode: Image.PreserveAspectFit
                        source: modelData.iconName
                            ? "../../assets/images/wx/" + modelData.iconName + ".svg"
                            : ""
                    }
                    MultiEffect {
                        anchors.fill: dayIconImage
                        source: dayIconImage
                        colorization: 1.0
                        colorizationColor: root.accentColor
                    }
                }

                // Anchored to the bottom of the column rather than flowing after
                // the condition, so LO/HI line up across all three days
                // regardless of how many lines the condition took.
                //
                // Centred as a *block*: the Column takes the width of its wider
                // line, so LO and HI stay left-aligned with each other and the
                // numbers still stack, rather than each line centring
                // independently and leaving the digits ragged.
                Column {
                    id: temps
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    spacing: root.sh * 0.0138021 //6.625

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

                // Between columns, not after the last one. Same rule and colour
                // as the status bar's horizontal divider, inset top and bottom
                // rather than running the full height.
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.max(1, root.sh * 0.003125) //2
                    color: root.tertiaryColor
                    opacity: 0.5
                    visible: index < screen.days.length - 1
                }
            }
        }
    }

    // Row has no implicit height from anchors alone; give the columns the space
    // between the title and the bottom of the screen area.
    Binding {
        target: columns
        property: "height"
        value: screen.height - title.height - (root.sh * 0.13125) //64.125
    }
}
