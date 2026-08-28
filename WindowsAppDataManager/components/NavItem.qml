import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AbstractButton {
    id: control

    required property url iconSource
    property url selectedIconSource
    property string label: ""
    property string badge: ""
    property bool selected: false

    implicitHeight: 44
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    background: Item {
        InsetStateLayer {
            anchors.fill: parent
            selected: control.selected
            hovered: control.hovered
            pressed: control.down
            focused: control.activeFocus
        }

        Rectangle {
            width: 4
            height: control.selected ? 26 : 0
            radius: 2
            color: Theme.accent
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter

            Behavior on height {
                NumberAnimation {
                    duration: control.selected ? Motion.fast : Motion.hoverExit
                    easing.type: Easing.OutCubic
                }
            }
        }

    }

    contentItem: RowLayout {
        spacing: 10

        Item {
            Layout.leftMargin: 13
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20

            ThemedIcon {
                width: 19
                height: 19
                anchors.centerIn: parent
                source: control.selected && control.selectedIconSource.toString().length > 0
                        ? control.selectedIconSource : control.iconSource
                color: !control.enabled ? Theme.textMuted
                       : control.selected ? Theme.accent : Theme.textSecondary
            }
        }

        Text {
            Layout.fillWidth: true
            text: control.label
            color: !control.enabled ? Theme.textMuted
                   : control.selected ? Theme.accentText : Theme.textSecondary
            font.pixelSize: 14
            font.weight: control.selected ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        Rectangle {
            visible: control.badge.length > 0
            implicitWidth: Math.max(22, badgeText.implicitWidth + 10)
            implicitHeight: 20
            radius: 10
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            Layout.rightMargin: 10
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
