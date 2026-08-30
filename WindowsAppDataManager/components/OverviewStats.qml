pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: summary

    required property ApplicationListModel applications

    implicitHeight: width < 900 ? 164 : 94
    radius: Theme.radiusMedium
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Grid {
        id: statsGrid

        anchors.fill: parent
        columns: summary.width < 900 ? 2 : 4
        rows: columns === 2 ? 2 : 1

        Repeater {
            model: 4

            delegate: Item {
                id: statDelegate

                required property int index
                readonly property url metricIcon: index === 0
                                                     ? Qt.resolvedUrl("../resources/Icons/TablerFileFilled.svg")
                                                     : index === 1
                                                       ? Qt.resolvedUrl("../resources/Icons/IcBaselineCleaningServices.svg")
                                                       : index === 2
                                                         ? Qt.resolvedUrl("../resources/Icons/TablerFilesFilled.svg")
                                                         : Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
                readonly property string metricLabel: index === 0 ? "AppData 占用"
                                                       : index === 1 ? "可重新生成"
                                                       : index === 2 ? "应用归属"
                                                                     : "需确认数据"
                readonly property string metricValue: index === 0
                                                       ? summary.applications.totalSizeText
                                                       : index === 1
                                                         ? summary.applications.reclaimableSizeText
                                                         : index === 2
                                                           ? summary.applications.count.toString()
                                                           : summary.applications.reviewSizeText
                readonly property string metricDetail: index === 0
                                                        ? "共 " + summary.applications.totalFileCountText + " 个文件"
                                                        : index === 1
                                                          ? "仍需逐项验证"
                                                          : index === 2
                                                            ? summary.applications.recognizedCount + " 个已识别"
                                                            : "包含高风险与未知数据"
                readonly property color metricAccent: index === 0 ? Theme.accent
                                                      : index === 1 ? Theme.green
                                                      : index === 2 ? Theme.purple
                                                                    : Theme.amber
                width: statsGrid.width / statsGrid.columns
                height: statsGrid.height / statsGrid.rows

                StatTile {
                    anchors.fill: parent
                    iconSource: statDelegate.metricIcon
                    label: statDelegate.metricLabel
                    value: statDelegate.metricValue
                    detail: statDelegate.metricDetail
                    accent: statDelegate.metricAccent
                }

                Rectangle {
                    visible: statDelegate.index % statsGrid.columns > 0
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: 54
                    color: Theme.divider
                }

                Rectangle {
                    visible: statsGrid.rows > 1 && statDelegate.index >= statsGrid.columns
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: Theme.divider
                }
            }
        }
    }
}
