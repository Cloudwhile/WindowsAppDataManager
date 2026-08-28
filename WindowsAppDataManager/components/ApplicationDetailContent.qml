pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Item {
    id: detail

    required property var application
    property bool paneMode: false
    readonly property bool potentialOrphan: application.installState === 1
    readonly property bool showOrphanAssessment: application.installState !== 0
                                                 && application.orphanSummary.length > 0

    signal focusRequested(Item item)

    function collectionItem(collection, index) {
        if (!collection)
            return ({})
        return collection.get ? collection.get(index) : collection[index]
    }

    implicitHeight: contentColumn.implicitHeight

    Column {
        id: contentColumn
        width: detail.width
        spacing: detail.paneMode ? 14 : 18

        RowLayout {
            width: parent.width
            spacing: 12

            Rectangle {
                Layout.preferredWidth: detail.paneMode ? 44 : 50
                Layout.preferredHeight: width
                radius: Theme.radiusMedium
                color: Theme.applicationAccent(detail.application.accentIndex)

                Text {
                    anchors.centerIn: parent
                    text: detail.application.shortName
                    color: Theme.applicationAccentText(detail.application.accentIndex)
                    font.pixelSize: detail.application.shortName.length > 2 ? 10 : 16
                    font.weight: Font.Bold
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: detail.application.appName
                    color: Theme.textPrimary
                    font.pixelSize: detail.paneMode ? 16 : 20
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: detail.application.publisher
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 4
                        color: detail.application.installState === 0 ? Theme.green
                               : detail.application.installState === 1 ? Theme.amber
                                                                      : Theme.neutral
                    }

                    Text {
                        Layout.fillWidth: true
                        text: detail.application.installStateText + " · " + detail.application.category
                        color: Theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
            }

            RiskBadge {
                level: detail.application.riskLevel
                label: detail.application.riskText
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        Grid {
            id: metricGrid
            width: parent.width
            columns: detail.width >= 680 ? 4 : 2
            columnSpacing: 12
            rowSpacing: 6

            Repeater {
                model: [
                    { "label": "AppData 占用", "value": detail.application.sizeText, "accent": Theme.accent },
                    { "label": "可重新生成", "value": detail.application.reclaimableText, "accent": Theme.green },
                    { "label": "受保护", "value": detail.application.protectedSizeText, "accent": Theme.purple },
                    { "label": "尚未分类", "value": detail.application.unknownSizeText, "accent": Theme.neutral }
                ]

                delegate: Item {
                    id: metricDelegate

                    required property var modelData
                    width: (metricGrid.width - metricGrid.columnSpacing * (metricGrid.columns - 1)) / metricGrid.columns
                    height: 48

                    DetailMetric {
                        anchors.fill: parent
                        label: metricDelegate.modelData.label
                        value: metricDelegate.modelData.value
                        accent: metricDelegate.modelData.accent
                    }
                }
            }
        }

        Column {
            width: parent.width
            spacing: 0

            Text {
                text: "数据分类"
                color: Theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
        }

        Column {
            width: parent.width
            spacing: 2

            Repeater {
                model: detail.application.dataGroups

                delegate: DataCategoryRow {
                    required property int index
                    width: parent.width
                    rowData: detail.collectionItem(detail.application.dataGroups, index)
                    onFocusRequested: item => detail.focusRequested(item)
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        Column {
            width: parent.width
            spacing: 9

            Text {
                text: "识别依据"
                color: Theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            ConfidenceIndicator {
                width: parent.width
                value: detail.application.confidence
            }

            ConfidenceIndicator {
                width: parent.width
                visible: detail.potentialOrphan
                label: "残留判断置信度"
                value: detail.application.orphanConfidence
            }

            Column {
                width: parent.width
                visible: detail.showOrphanAssessment
                spacing: 7

                Text {
                    width: parent.width
                    text: detail.application.orphanSummary
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: detail.application.orphanBlockingReasons

                    delegate: RowLayout {
                        id: orphanReason

                        required property string modelData
                        width: parent.width
                        spacing: 7

                        ThemedIcon {
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            Layout.alignment: Qt.AlignTop
                            source: Qt.resolvedUrl("../resources/Icons/TablerAlertSmall.svg")
                            color: Theme.amberText
                        }

                        Text {
                            Layout.fillWidth: true
                            text: orphanReason.modelData
                            color: Theme.textMuted
                            font.pixelSize: 10
                            lineHeight: 1.3
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Repeater {
                model: detail.application.evidence

                delegate: EvidenceRow {
                    required property int index
                    width: parent.width
                    rowData: detail.collectionItem(detail.application.evidence, index)
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        Column {
            width: parent.width
            spacing: 8

            Text {
                text: "位置与安装信息"
                color: Theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            PathField {
                width: parent.width
                label: "AppData 位置"
                value: detail.application.location
                onFocusRequested: item => detail.focusRequested(item)
            }
            PathField {
                width: parent.width
                label: "可执行文件"
                value: detail.application.executablePath
                onFocusRequested: item => detail.focusRequested(item)
            }
            PathField {
                width: parent.width
                label: "安装位置"
                value: detail.application.installPath
                onFocusRequested: item => detail.focusRequested(item)
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        Row {
            width: parent.width
            spacing: 10

            Rectangle {
                width: 3
                height: summaryText.implicitHeight
                radius: 2
                color: detail.application.riskLevel >= 3 ? Theme.red
                       : detail.application.riskLevel === 2 ? Theme.amber : Theme.accent
            }

            Text {
                id: summaryText
                width: parent.width - 13
                text: detail.application.summary
                color: Theme.textSecondary
                font.pixelSize: 11
                lineHeight: 1.35
                wrapMode: Text.WordWrap
            }
        }
    }
}
