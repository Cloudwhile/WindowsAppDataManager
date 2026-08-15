import QtQuick
import QtQuick.Controls

Item {
    id: field

    property string label: ""
    property string value: ""

    implicitHeight: value.length > 0 ? 43 : 0
    visible: implicitHeight > 0

    Column {
        anchors.fill: parent
        spacing: 3

        Text {
            width: parent.width
            text: field.label
            color: Theme.textMuted
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }

        Text {
            id: pathText
            width: parent.width
            text: field.value
            color: Theme.textSecondary
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    ToolTip.visible: hoverArea.containsMouse && field.value.length > 0
    ToolTip.text: field.value
    ToolTip.delay: 500
}
