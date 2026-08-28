import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control

    required property url iconSource
    property string tooltip: ""
    property bool prominent: false
    property int iconSize: 18
    property color symbolColor: prominent ? Theme.onAccent : Theme.textSecondary

    implicitWidth: 36
    implicitHeight: 34
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    background: InsetStateLayer {
        inset: control.prominent ? 0 : 4
        selected: control.prominent
        hovered: control.hovered && !control.prominent
        pressed: control.down
        focused: control.activeFocus
        selectedColor: Theme.accent
        pressedColor: control.prominent ? Theme.accentStrong : Theme.surfaceSelected
        focusColor: control.prominent ? Theme.onAccent : Theme.accent
        focusWidth: 2
        idleBorderWidth: !control.prominent && control.hovered ? 1 : 0
        idleBorderColor: Theme.border
    }

    contentItem: Item {
        ThemedIcon {
            width: control.iconSize
            height: control.iconSize
            anchors.centerIn: parent
            source: control.iconSource
            color: control.enabled ? control.symbolColor : Theme.textMuted
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: tooltip

    scale: control.down && Motion.allowScale ? 0.97 : 1.0
    Behavior on scale {
        NumberAnimation { duration: Motion.instant; easing.type: Easing.OutCubic }
    }

    ToolTip.visible: hovered && tooltip.length > 0
    ToolTip.text: tooltip
    ToolTip.delay: 450
}
