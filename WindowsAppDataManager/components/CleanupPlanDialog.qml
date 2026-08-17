pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    property var items: []
    property string totalText: "0 B"

    modal: true
    focus: true
    width: Math.min(760, parent ? parent.width - 56 : 760)
    height: Math.min(640, parent ? parent.height - 72 : 640)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }

    header: Rectangle {
        implicitHeight: 58
        color: Theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 8
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: Theme.radiusSmall
                color: Theme.greenSoft

                ThemedIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: Qt.resolvedUrl("../resources/Icons/IcBaselineCleaningServices.svg")
                    color: Theme.greenText
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    text: "清理计划"
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }

                Text {
                    text: dialog.items.length + " 项候选，合计 " + dialog.totalText
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
            }

            IconButton {
                iconSource: Qt.resolvedUrl("../resources/Icons/TablerX.svg")
                tooltip: "关闭"
                onClicked: dialog.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 10
            implicitHeight: previewNotice.implicitHeight + 16
            radius: Theme.radiusSmall
            color: Theme.accentSoft

            Text {
                id: previewNotice
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                text: "这是只读预览，不会删除任何文件。仅列出规则明确、可重新生成，且风险为安全或低风险的数据。"
                color: Theme.accentText
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        ListView {
            id: planList

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            clip: true
            spacing: 8
            model: dialog.items
            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
                id: planRow

                required property var modelData
                width: planList.width
                height: planContent.implicitHeight + 18
                radius: Theme.radiusMedium
                color: Theme.surfaceRaised
                border.width: 1
                border.color: Theme.border

                Column {
                    id: planContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        width: parent.width
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: planRow.modelData.applicationName + " · "
                                  + planRow.modelData.categoryText
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text {
                            text: planRow.modelData.sizeText
                            color: Theme.textPrimary
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }

                        RiskBadge {
                            level: planRow.modelData.riskLevel
                            label: planRow.modelData.riskText
                        }
                    }

                    Text {
                        width: parent.width
                        text: planRow.modelData.impact
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    PathField {
                        width: parent.width
                        label: "路径"
                        value: planRow.modelData.path
                    }

                    Text {
                        width: parent.width
                        text: "依据  " + planRow.modelData.ruleSource
                        color: Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: planList.count === 0
                text: "当前没有符合安全边界的可重建数据。"
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }
    }
}
