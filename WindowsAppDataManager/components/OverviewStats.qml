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
            model: [
                { "icon": Qt.resolvedUrl("../resources/Icons/TablerFileFilled.svg"), "label": "AppData 占用", "value": summary.applications.totalSizeText, "detail": "共 " + summary.applications.totalFileCountText + " 个文件", "accent": Theme.accent },
                { "icon": Qt.resolvedUrl("../resources/Icons/IcBaselineCleaningServices.svg"), "label": "可重新生成", "value": summary.applications.reclaimableSizeText, "detail": "仍需逐项验证", "accent": Theme.green },
                { "icon": Qt.resolvedUrl("../resources/Icons/TablerFilesFilled.svg"), "label": "应用归属", "value": summary.applications.count.toString(), "detail": summary.applications.recognizedCount + " 个已识别", "accent": Theme.purple },
                { "icon": Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg"), "label": "需确认数据", "value": summary.applications.reviewSizeText, "detail": "包含高风险与未知数据", "accent": Theme.amber }
            ]

            delegate: Item {
                id: statDelegate

                required property int index
                required property var modelData
                width: statsGrid.width / statsGrid.columns
                height: statsGrid.height / statsGrid.rows

                StatTile {
                    anchors.fill: parent
                    iconSource: statDelegate.modelData.icon
                    label: statDelegate.modelData.label
                    value: statDelegate.modelData.value
                    detail: statDelegate.modelData.detail
                    accent: statDelegate.modelData.accent
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
