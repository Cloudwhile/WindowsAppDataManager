pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: panel

    required property real progress
    required property string currentPath
    required property string status
    required property ApplicationListModel applications
    required property int issueCount
    required property bool active

    readonly property real normalizedProgress: Math.max(0, Math.min(100, progress))
    readonly property bool compact: width < 690

    implicitHeight: compact ? 268 : 238
    radius: Theme.radiusLarge
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 24

        ScanRadar {
            Layout.preferredWidth: 188
            Layout.preferredHeight: 188
            Layout.alignment: Qt.AlignVCenter
            visible: !panel.compact
            progress: panel.normalizedProgress
            status: panel.status
            active: panel.active
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Text {
                text: panel.status
                color: Theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 15

                Text {
                    text: panel.active ? "扫描中"
                                       : panel.normalizedProgress >= 100
                                         ? "扫描完成" : "等待扫描"
                    color: Theme.accent
                    font.pixelSize: 34
                    font.weight: Font.Bold
                }

                ScanProgressBar {
                    id: overviewProgress

                    Layout.fillWidth: true
                    Layout.preferredHeight: 7
                    progress: panel.normalizedProgress
                    active: panel.active
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ThemedIcon {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: Qt.resolvedUrl("../resources/Icons/TablerFileFilled.svg")
                    color: Theme.accent
                }

                Text {
                    Layout.fillWidth: true
                    text: panel.currentPath.length > 0 ? panel.currentPath : panel.status
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            GridLayout {
                id: metricGrid

                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: panel.compact ? 2 : 4
                columnSpacing: 0
                rowSpacing: 0

                Repeater {
                    model: 4

                    delegate: Item {
                        id: metric

                        required property int index
                        readonly property url metricIcon: index === 0
                                                             ? Qt.resolvedUrl("../resources/Icons/TablerFiles.svg")
                                                             : index === 1
                                                               ? Qt.resolvedUrl("../resources/Icons/TablerFileFilled.svg")
                                                               : index === 2
                                                                 ? Qt.resolvedUrl("../resources/Icons/TablerChartPieFilled.svg")
                                                                 : Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
                        readonly property string metricLabel: index === 0 ? "已分析应用"
                                                               : index === 1 ? "已分析文件"
                                                               : index === 2 ? "已分析占用"
                                                                             : "读取问题"
                        readonly property string metricValue: index === 0
                                                               ? panel.applications.count.toString()
                                                               : index === 1
                                                                 ? panel.applications.totalFileCountText
                                                                 : index === 2
                                                                   ? panel.applications.totalSizeText
                                                                   : panel.issueCount.toString()
                        readonly property color metricAccent: index === 0 ? Theme.purple
                                                              : index === 1 ? Theme.cyan
                                                              : index === 2 ? Theme.green
                                                              : panel.issueCount > 0
                                                                ? Theme.amber : Theme.neutral

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 43

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: metric.index % metricGrid.columns === 0 ? 0 : 12
                            anchors.rightMargin: 8
                            spacing: 7

                            ThemedIcon {
                                Layout.preferredWidth: 17
                                Layout.preferredHeight: 17
                                source: metric.metricIcon
                                color: metric.metricAccent
                            }

                            Column {
                                Layout.fillWidth: true
                                spacing: 1

                                Text {
                                    width: parent.width
                                    text: metric.metricLabel
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: metric.metricValue
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        Rectangle {
                            visible: metric.index % metricGrid.columns > 0
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 1
                            height: 32
                            color: Theme.divider
                        }
                    }
                }
            }
        }
    }
}
