import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AbstractButton {
    id: control

    required property string title
    required property string description
    required property url iconSource
    property bool selected: false
    property color accent: Theme.accent

    signal focusRequested(Item item)

    implicitHeight: 64
    hoverEnabled: true
    checkable: true
    autoExclusive: true
    checked: selected
    onActiveFocusChanged: {
        if (activeFocus)
            focusRequested(control)
    }

    background: InsetStateLayer {
        selected: control.selected
        hovered: control.hovered
        pressed: control.down
        focused: control.activeFocus
    }

    contentItem: RowLayout {
        spacing: 12

        Rectangle {
            Layout.leftMargin: 10
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: Theme.radiusSmall
            color: Qt.rgba(control.accent.r, control.accent.g, control.accent.b,
                           Theme.dark ? 0.2 : 0.12)

            ThemedIcon {
                width: 18
                height: 18
                anchors.centerIn: parent
                source: control.iconSource
                color: control.accent
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: control.title
                color: Theme.textPrimary
                font.pixelSize: 13
                font.weight: control.selected ? Font.DemiBold : Font.Medium
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: control.description
                color: Theme.textMuted
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        Rectangle {
            Layout.rightMargin: 12
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            radius: 10
            color: control.selected ? Theme.accent : "transparent"
            border.width: control.selected ? 0 : 1
            border.color: Theme.border

            ThemedIcon {
                width: 12
                height: 12
                anchors.centerIn: parent
                visible: control.selected
                source: Qt.resolvedUrl("../resources/Icons/TablerCheck.svg")
                color: Theme.onAccent
            }
        }
    }

    Accessible.role: Accessible.RadioButton
    Accessible.name: title
    Accessible.description: description
    Accessible.checked: checked
}
