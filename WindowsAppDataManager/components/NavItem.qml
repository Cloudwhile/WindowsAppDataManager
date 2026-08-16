import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control

    required property url iconSource
    property url selectedIconSource
    property string label: ""
    property string badge: ""
    property bool selected: false

    implicitHeight: 40
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.selected ? Theme.surfaceSelected
                                : control.hovered ? Theme.surfaceHover : "transparent"

        Rectangle {
            width: 3
            height: control.selected ? 22 : 0
            radius: 2
            color: Theme.accent
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter

            Behavior on height {
                NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic }
            }
        }

        Behavior on color {
            ColorAnimation { duration: Motion.fast }
        }
    }

    contentItem: Row {
        leftPadding: 13
        rightPadding: 10
        spacing: 11

        Item {
            width: 20
            height: parent.height
            anchors.verticalCenter: parent.verticalCenter

            ThemedIcon {
                width: 18
                height: 18
                anchors.centerIn: parent
                source: control.selected && control.selectedIconSource.toString().length > 0
                        ? control.selectedIconSource : control.iconSource
                color: !control.enabled ? Theme.textMuted
                       : control.selected ? Theme.accent : Theme.textSecondary
            }
        }

        Text {
            width: Math.max(0, control.width - 78)
            anchors.verticalCenter: parent.verticalCenter
            text: control.label
            color: !control.enabled ? Theme.textMuted
                   : control.selected ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: 14
            font.weight: control.selected ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        Rectangle {
            visible: control.badge.length > 0
            width: Math.max(22, badgeText.implicitWidth + 10)
            height: 20
            radius: 10
            anchors.verticalCenter: parent.verticalCenter
            color: control.enabled ? Theme.redSoft : Theme.neutralSoft

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: control.badge
                color: control.enabled ? Theme.redText : Theme.textMuted
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: label
}
