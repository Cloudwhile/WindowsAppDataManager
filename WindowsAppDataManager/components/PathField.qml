import QtQuick
import QtQuick.Controls

Item {
    id: field

    property string label: ""
    property string value: ""

    signal focusRequested(Item item)

    implicitHeight: value.length > 0 ? 45 : 0
    visible: implicitHeight > 0

    Column {
        anchors.fill: parent
        spacing: 2

        Text {
            width: parent.width
            text: field.label
            color: Theme.textMuted
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }

        TextField {
            id: pathText

            width: parent.width
            height: 25
            text: field.value
            readOnly: true
            selectByMouse: true
            activeFocusOnTab: true
            color: Theme.textSecondary
            selectionColor: Theme.accent
            selectedTextColor: Theme.onAccent
            font.pixelSize: 11
            leftPadding: 2
            rightPadding: 2
            topPadding: 0
            bottomPadding: 0
            clip: true
            onActiveFocusChanged: {
                if (activeFocus)
                    field.focusRequested(pathText)
            }

            background: Rectangle {
                color: "transparent"
                border.width: pathText.activeFocus ? 1 : 0
                border.color: Theme.accent
                radius: Theme.radiusSmall
            }

            Accessible.role: Accessible.EditableText
            Accessible.name: field.label
            Accessible.description: field.value
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
