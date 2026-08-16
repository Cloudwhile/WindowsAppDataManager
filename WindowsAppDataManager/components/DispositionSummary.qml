pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: summary

    required property ApplicationListModel applications

    implicitHeight: 156
    radius: Theme.radiusMedium
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.top: parent.top
        anchors.topMargin: 14
        text: "处置判定分布"
        color: Theme.textPrimary
        font.pixelSize: 14
        font.weight: Font.DemiBold
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.top: parent.top
        anchors.topMargin: 16
        text: "按安全边界聚合，不代表清理计划"
        color: Theme.textMuted
        font.pixelSize: 10
    }

    Rectangle {
        id: distributionTrack

        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.top: parent.top
        anchors.topMargin: 52
        height: 10
        radius: 4
        color: Theme.divider
        clip: true

        Row {
            anchors.fill: parent

            Repeater {
                model: [
                    { "ratio": summary.applications.reclaimableRatio, "color": Theme.green },
                    { "ratio": summary.applications.protectedRatio, "color": Theme.purple },
                    { "ratio": summary.applications.reviewRatio, "color": Theme.amber }
                ]

                delegate: Rectangle {
                    id: segment

                    required property var modelData
                    width: distributionTrack.width * Math.max(0, segment.modelData.ratio) / 100
                    height: distributionTrack.height
                    color: segment.modelData.color

                    Behavior on width {
                        NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic }
                    }
                }
            }
        }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.top: distributionTrack.bottom
        anchors.topMargin: 16

        Repeater {
            model: [
                { "label": "可重新生成", "value": summary.applications.reclaimableSizeText, "ratio": summary.applications.reclaimableRatio.toFixed(1) + "%", "color": Theme.green },
                { "label": "受保护", "value": summary.applications.protectedSizeText, "ratio": summary.applications.protectedRatio.toFixed(1) + "%", "color": Theme.purple },
                { "label": "需确认 / 未知", "value": summary.applications.reviewSizeText, "ratio": summary.applications.reviewRatio.toFixed(1) + "%", "color": Theme.amber }
            ]

            delegate: Item {
                id: metric

                required property int index
                required property var modelData
                width: parent.width / 3
                height: 54

                Rectangle {
                    visible: metric.index > 0
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: 34
                    color: Theme.divider
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: metric.index > 0 ? 18 : 0
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Rectangle {
                        width: 8
                        height: 8
                        radius: 2
                        anchors.verticalCenter: parent.verticalCenter
                        color: metric.modelData.color
                    }

                    Column {
                        width: parent.width - 16
                        spacing: 2

                        Text {
                            width: parent.width
                            text: metric.modelData.label + "  " + metric.modelData.ratio
                            color: Theme.textPrimary
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: metric.modelData.value
                            color: Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
