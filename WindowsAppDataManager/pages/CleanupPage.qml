pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page

    required property CleanupViewModel cleanupController
    property bool scanning: false
    signal scanRequested()

    readonly property CleanupPlanModel plan: cleanupController.items

    onScanningChanged: {
        if (scanning && confirmation.opened)
            confirmation.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: 20
        anchors.bottomMargin: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: "清理计划"
                    color: Theme.textPrimary
                    font.pixelSize: 25
                    font.weight: Font.DemiBold
                }

                Text {
                    Layout.fillWidth: true
                    text: page.cleanupController.statusText
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            IconButton {
                iconSource: Qt.resolvedUrl("../resources/Icons/TablerRefresh.svg")
                tooltip: "重新生成清理计划"
                enabled: page.cleanupController.hasScan
                         && !page.cleanupController.running
                         && !page.scanning
                onClicked: page.cleanupController.rebuildPlan()
            }

            IconButton {
                iconSource: page.cleanupController.running
                            ? Qt.resolvedUrl("../resources/Icons/TablerCancel.svg")
                            : Qt.resolvedUrl("../resources/Icons/TablerTrashFilled.svg")
                tooltip: page.cleanupController.running ? "停止清理" : "执行所选清理"
                prominent: true
                enabled: page.cleanupController.running
                         || (!page.scanning && page.cleanupController.canExecute)
                onClicked: {
                    if (page.cleanupController.running)
                        page.cleanupController.cancel()
                    else
                        confirmation.open()
                }
            }
        }

        InlineNotice {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: page.cleanupController.errorMessage.length > 0
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
            message: page.cleanupController.errorMessage
            accent: Theme.redText
            fill: Theme.redSoft
            actionIconSource: Qt.resolvedUrl("../resources/Icons/TablerRefresh.svg")
            actionTooltip: "重新生成清理计划"
            actionVisible: page.cleanupController.hasScan
                           && !page.cleanupController.running
                           && !page.scanning
            onActionRequested: page.cleanupController.rebuildPlan()
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 18

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: page.plan.selectedCount + " 项已选择 · "
                              + page.plan.selectedSizeText
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: page.plan.excludedCount > 0
                              ? page.plan.excludedCount + " 项不符合本次清理条件，未加入计划"
                              : "当前计划中的内容均可重新生成"
                        color: page.plan.excludedCount > 0
                               ? Theme.amberText : Theme.textSecondary
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

                ColumnLayout {
                    spacing: 2

                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: page.cleanupController.running
                              ? "本次已移入回收站"
                              : "最近移入回收站 " + page.cleanupController.lastCleanupText
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }

                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: page.cleanupController.running
                              ? page.plan.releasedSizeText
                              : page.cleanupController.lastReleasedSizeText
                        color: Theme.greenText
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 220
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    spacing: 10

                    Text {
                        Layout.leftMargin: 14
                        Layout.fillWidth: true
                        text: "已验证清理项目"
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.rightMargin: 14
                        text: "预计 " + page.plan.estimatedSizeText
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.divider
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: page.plan
                    ScrollBar.vertical: ScrollBar { }

                    delegate: CleanupItemRow {
                        width: ListView.view.width
                        selectionEnabled: !page.scanning
                                          && !page.cleanupController.running
                        onSelectionChanged: (itemIndex, selected) =>
                                            page.plan.setSelected(itemIndex, selected)
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: page.plan.count === 0
                width: Math.min(parent.width - 40, 520)
                height: Math.min(parent.height - 20, 260)
                iconSource: page.scanning
                            ? Qt.resolvedUrl("../resources/Icons/TablerActivityHeartbeat.svg")
                            : page.cleanupController.hasScan
                              ? Qt.resolvedUrl("../resources/Icons/TablerCheck.svg")
                              : Qt.resolvedUrl("../resources/Icons/TablerZoomScan.svg")
                title: page.scanning ? "正在更新清理候选"
                                     : page.cleanupController.hasScan
                                       ? "没有可执行的安全清理项"
                                       : "需要先完成一次扫描"
                description: page.scanning ? "候选项目正在更新。"
                             : page.cleanupController.hasScan
                               ? "当前没有符合低风险清理条件的项目。"
                               : "当前没有可用的扫描结果。"
                actionIconSource: Qt.resolvedUrl("../resources/Icons/TablerPlayerPlayFilled.svg")
                actionTooltip: "开始扫描"
                actionVisible: !page.cleanupController.hasScan && !page.scanning
                onActionRequested: page.scanRequested()
            }
        }
    }

    Dialog {
        id: confirmation

        anchors.centerIn: parent
        width: Math.min(440, page.width - 48)
        modal: true
        closePolicy: Popup.CloseOnEscape
        title: "确认清理所选项目"
        onOpened: cancelButton.forceActiveFocus()

        contentItem: Text {
            width: confirmation.availableWidth
            text: "将 " + page.plan.selectedCount + " 项、共 "
                  + page.plan.selectedSizeText
                  + " 移动到 Windows 回收站。之后可从回收站恢复这些内容。"
            color: Theme.textSecondary
            font.pixelSize: 12
            lineHeight: 1.4
            wrapMode: Text.WordWrap
        }

        footer: Item {
            implicitHeight: 48

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                IconButton {
                    id: cancelButton

                    iconSource: Qt.resolvedUrl("../resources/Icons/TablerX.svg")
                    tooltip: "取消"
                    onClicked: confirmation.close()
                }

                IconButton {
                    iconSource: Qt.resolvedUrl("../resources/Icons/TablerTrashFilled.svg")
                    tooltip: "确认移动到回收站"
                    prominent: true
                    enabled: page.cleanupController.canExecute
                             && !page.cleanupController.running
                             && !page.scanning
                    onClicked: {
                        confirmation.close()
                        page.cleanupController.executeSelected()
                    }
                }
            }
        }
    }
}
