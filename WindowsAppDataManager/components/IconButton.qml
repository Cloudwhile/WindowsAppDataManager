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

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.prominent
               ? (control.down ? Theme.accentStrong : Theme.accent)
               : control.down ? Theme.surfaceSelected
                              : control.hovered ? Theme.surfaceHover : "transparent"
        border.width: control.prominent || !control.hovered ? 0 : 1
        border.color: Theme.border

        Behavior on color {
            ColorAnimation { duration: Motion.fast }
        }
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
