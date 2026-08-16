import QtQuick
import QtQuick.Layouts

Rectangle {
    id: categoryRow

    required property var rowData
    property bool expanded: false

    function toggleExpanded() {
        expanded = !expanded
    }

    implicitHeight: contentColumn.implicitHeight + 20
    radius: Theme.radiusSmall
    color: activeFocus || mouseArea.containsMouse ? Theme.surfaceHover : "transparent"
    clip: true
    activeFocusOnTab: true
    border.width: activeFocus ? 1 : 0
    border.color: Theme.accent

    Accessible.role: Accessible.Button
    Accessible.name: rowData.categoryText + "，" + rowData.sizeText + "，"
                     + rowData.riskText + "，" + (expanded ? "已展开" : "已折叠")
    Accessible.description: rowData.impactText
    Accessible.onPressAction: categoryRow.toggleExpanded()

    Behavior on color {
        ColorAnimation { duration: Motion.fast }
    }

    Column {
        id: contentColumn
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.top: parent.top
        anchors.topMargin: 10
        spacing: 7

        RowLayout {
            width: parent.width
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: categoryRow.rowData.categoryText
                color: Theme.textPrimary
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                text: categoryRow.rowData.sizeText
                color: Theme.textPrimary
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            RiskBadge {
                level: categoryRow.rowData.riskLevel
                label: categoryRow.rowData.riskText
            }

            ThemedIcon {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                source: Qt.resolvedUrl("../resources/Icons/TablerChevronDownFilled.svg")
                color: Theme.textMuted
                rotation: Motion.allowPosition && categoryRow.expanded ? 180 : 0

                Behavior on rotation {
                    NumberAnimation {
                        duration: Motion.allowPosition ? Motion.fast : 0
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 5
            radius: 2
            color: Theme.divider

            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, categoryRow.rowData.ratio))
                height: parent.height
                radius: parent.radius
                color: categoryRow.rowData.riskLevel === 0 ? Theme.green
                       : categoryRow.rowData.riskLevel === 1 ? Theme.accent
                       : categoryRow.rowData.riskLevel === 2 ? Theme.amber
                       : categoryRow.rowData.riskLevel === 3 ? Theme.red
                       : categoryRow.rowData.riskLevel === 4 ? Theme.purple
                                                           : Theme.neutral

                Behavior on width {
                    NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic }
                }
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: categoryRow.rowData.impactText
                color: Theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            Text {
                text: categoryRow.rowData.rebuildableText
                color: categoryRow.rowData.rebuildableState === 0 ? Theme.greenText
                       : categoryRow.rowData.rebuildableState === 1 ? Theme.textSecondary
                                                                   : Theme.neutral
                font.pixelSize: 10
                font.weight: Font.Medium
            }
        }

        Item {
            width: parent.width
            height: categoryRow.expanded ? detailColumn.implicitHeight : 0
            opacity: categoryRow.expanded ? 1 : 0
            clip: true

            Column {
                id: detailColumn
                width: parent.width
                spacing: 4

                Text {
                    width: parent.width
                    text: "路径  " + categoryRow.rowData.path
                    color: Theme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }

                Text {
                    width: parent.width
                    text: "依据  " + categoryRow.rowData.ruleSource
                    color: Theme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            Behavior on height {
                NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic }
            }
            Behavior on opacity {
                NumberAnimation { duration: Motion.fast }
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            categoryRow.forceActiveFocus()
            categoryRow.toggleExpanded()
        }
    }

    Keys.onReturnPressed: categoryRow.toggleExpanded()
    Keys.onEnterPressed: categoryRow.toggleExpanded()
    Keys.onSpacePressed: categoryRow.toggleExpanded()
}
