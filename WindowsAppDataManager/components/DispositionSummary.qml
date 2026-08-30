pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: summary

    required property ApplicationListModel applications
    required property bool active

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
                model: 3

                delegate: Rectangle {
                    id: segment

                    required property int index
                    readonly property real ratio: index === 0
                                                  ? summary.applications.reclaimableRatio
                                                  : index === 1
                                                    ? summary.applications.protectedRatio
                                                    : summary.applications.reviewRatio
                    readonly property color segmentColor: index === 0 ? Theme.green
                                                          : index === 1 ? Theme.purple
                                                                        : Theme.amber
                    width: distributionTrack.width * Math.max(0, segment.ratio) / 100
                    height: distributionTrack.height
                    color: segment.segmentColor

                    Behavior on width {
                        enabled: !summary.active
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
            model: 3

            delegate: Item {
                id: metric

                required property int index
                readonly property string metricLabel: index === 0 ? "可重新生成"
                                                       : index === 1 ? "受保护"
                                                                     : "需确认 / 未知"
                readonly property string metricValue: index === 0
                                                       ? summary.applications.reclaimableSizeText
                                                       : index === 1
                                                         ? summary.applications.protectedSizeText
                                                         : summary.applications.reviewSizeText
                readonly property real metricRatio: index === 0
                                                    ? summary.applications.reclaimableRatio
                                                    : index === 1
                                                      ? summary.applications.protectedRatio
                                                      : summary.applications.reviewRatio
                readonly property color metricColor: index === 0 ? Theme.green
                                                     : index === 1 ? Theme.purple
                                                                   : Theme.amber
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
                        color: metric.metricColor
                    }

                    Column {
                        width: parent.width - 16
                        spacing: 2

                        Text {
                            width: parent.width
                            text: metric.metricLabel + "  "
                                  + metric.metricRatio.toFixed(1) + "%"
                            color: Theme.textPrimary
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: metric.metricValue
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
