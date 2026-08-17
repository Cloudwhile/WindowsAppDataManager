import QtQuick

Item {
    id: indicator

    property int value: 0
    property string label: "识别置信度"

    implicitHeight: 42

    Column {
        anchors.fill: parent
        spacing: 7

        Row {
            width: parent.width

            Text {
                width: parent.width - 58
                text: indicator.label
                color: Theme.textSecondary
                font.pixelSize: 11
            }

            Text {
                width: 58
                text: indicator.value + "%"
                color: indicator.value >= 85 ? Theme.greenText
                       : indicator.value >= 60 ? Theme.amberText : Theme.redText
                font.pixelSize: 12
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
            }
        }

        Rectangle {
            width: parent.width
            height: 6
            radius: 3
            color: Theme.divider

            Rectangle {
                width: parent.width * Math.max(0, Math.min(100, indicator.value)) / 100
                height: parent.height
                radius: parent.radius
                color: indicator.value >= 85 ? Theme.green
                       : indicator.value >= 60 ? Theme.amber : Theme.red

                Behavior on width {
                    NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic }
                }
            }
        }
    }
}
