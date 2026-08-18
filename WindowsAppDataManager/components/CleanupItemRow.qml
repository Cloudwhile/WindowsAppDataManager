pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: row

    required property int index
    required property string applicationName
    required property string categoryText
    required property string path
    required property string sizeText
    required property string impact
    required property bool selected
    required property int state
    required property string stateText
    required property string statusMessage
    property bool selectionEnabled: true

    signal selectionChanged(int index, bool selected)

    readonly property bool pending: state === 0
    readonly property color stateColor: state === 4 ? Theme.greenText
                                       : state === 6 ? Theme.redText
                                       : state === 1 || state === 2 || state === 3
                                         ? Theme.accentText : Theme.textMuted

    height: 68

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        CheckBox {
            Layout.preferredWidth: 26
            Layout.preferredHeight: 26
            checked: row.selected
            enabled: row.pending && row.selectionEnabled
            Accessible.name: (checked ? "取消选择 " : "选择 ")
                             + row.applicationName + " " + row.categoryText
            Accessible.description: row.sizeText + "，" + row.stateText + "。"
                                    + (row.statusMessage.length > 0
                                       ? row.statusMessage : row.impact)
                                    + "。路径 " + row.path
            onToggled: row.selectionChanged(row.index, checked)
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: row.applicationName + " · " + row.categoryText
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                Text {
                    text: row.sizeText
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }

            Text {
                id: statusText

                Layout.fillWidth: true
                text: row.statusMessage.length > 0 ? row.statusMessage : row.impact
                color: row.statusMessage.length > 0
                       ? row.stateColor : Theme.textSecondary
                font.pixelSize: 10
                elide: Text.ElideRight

                HoverHandler { id: statusHover }
                ToolTip.visible: statusHover.hovered && statusText.truncated
                ToolTip.text: statusText.text
                ToolTip.delay: 450
            }

            Text {
                id: pathText

                Layout.fillWidth: true
                visible: row.width > 620
                text: row.path
                color: Theme.textMuted
                font.pixelSize: 9
                elide: Text.ElideMiddle

                HoverHandler { id: pathHover }
                ToolTip.visible: pathHover.hovered && pathText.truncated
                ToolTip.text: pathText.text
                ToolTip.delay: 450
            }
        }

        Text {
            Layout.preferredWidth: 72
            text: row.stateText
            color: row.stateColor
            font.pixelSize: 10
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignRight
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.divider
    }
}
