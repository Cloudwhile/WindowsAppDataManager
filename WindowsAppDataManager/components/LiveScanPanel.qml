pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: panel

    required property real progress
    required property string currentPath
    required property string status
    required property ApplicationListModel applications
    required property var recentPaths
    required property int issueCount
    required property bool active

    signal detailsRequested()

    readonly property real normalizedProgress: Math.max(0, Math.min(100, progress))
    readonly property var visibleRecentPaths: {
        const paths = []
        for (let index = 0; index < recentPaths.length && paths.length < 3; ++index) {
            const path = recentPaths[index]
            if (path && path !== currentPath && paths.indexOf(path) < 0)
                paths.push(path)
        }
        return paths
    }

    function pathName(path) {
        const normalized = path.replace(/[\\/]+$/, "")
        const parts = normalized.split(/[\\/]/)
        return parts.length > 0 && parts[parts.length - 1].length > 0
                ? parts[parts.length - 1] : path
    }

    implicitWidth: 310
    implicitHeight: content.implicitHeight + 32
    radius: Theme.radiusLarge
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 13

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: panel.active ? "实时状态" : "当前状态"
                color: Theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            ThemedIcon {
                Layout.preferredWidth: 15
                Layout.preferredHeight: 15
                source: panel.active
                        ? Qt.resolvedUrl("../resources/Icons/TablerRefresh.svg")
                        : Qt.resolvedUrl("../resources/Icons/TablerPointFilled.svg")
                color: panel.active ? Theme.accent : Theme.textMuted
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 11

            ThemedIcon {
                Layout.preferredWidth: 25
                Layout.preferredHeight: 25
                source: Qt.resolvedUrl("../resources/Icons/TablerFileFilled.svg")
                color: Theme.amber
            }

            Column {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    width: parent.width
                    text: panel.currentPath.length > 0
                          ? panel.pathName(panel.currentPath) : panel.status
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: panel.currentPath
                    color: Theme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                    visible: text.length > 0
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ThemedIcon {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        source: Qt.resolvedUrl("../resources/Icons/TablerActivityHeartbeat.svg")
                        color: Theme.accent
                    }

                    Text {
                        text: "扫描进度"
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                }

                Text {
                    text: panel.active ? "实时更新"
                                       : panel.normalizedProgress >= 100
                                         ? "扫描完成" : "待命"
                    color: panel.active ? Theme.accent : Theme.textSecondary
                    font.pixelSize: 20
                    font.weight: Font.Bold
                }
            }

            ScanProgressBar {
                id: liveProgress

                Layout.fillWidth: true
                Layout.preferredHeight: 6
                progress: panel.normalizedProgress
                active: panel.active
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ThemedIcon {
                Layout.preferredWidth: 21
                Layout.preferredHeight: 21
                source: panel.issueCount > 0
                        ? Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
                        : panel.active
                        ? Qt.resolvedUrl("../resources/Icons/TablerActivityHeartbeat.svg")
                        : Qt.resolvedUrl("../resources/Icons/TablerCheck.svg")
                color: panel.issueCount > 0 ? Theme.amber
                                            : panel.active ? Theme.accent
                                                           : Theme.green
            }

            Column {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: "已分析结果"
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                Text {
                    text: panel.applications.count + " 个应用 · "
                          + panel.applications.totalSizeText
                    color: Theme.textSecondary
                    font.pixelSize: 10
                }
            }

            Text {
                text: "问题 " + panel.issueCount
                color: panel.issueCount > 0 ? Theme.amberText : Theme.greenText
                font.pixelSize: 11
                font.weight: Font.Bold
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: panel.active ? "本次扫描位置" : "最近扫描位置"
                color: Theme.textPrimary
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }

            IconButton {
                visible: panel.applications.count > 0
                iconSource: Qt.resolvedUrl("../resources/Icons/TablerChevronRight.svg")
                tooltip: "查看分析结果"
                onClicked: panel.detailsRequested()
            }
        }

        Column {
            Layout.fillWidth: true
            spacing: 0

            Repeater {
                model: 3

                delegate: RowLayout {
                    id: recentPathRow

                    required property int index
                    readonly property string path: index < panel.visibleRecentPaths.length
                                                   ? panel.visibleRecentPaths[index] : ""

                    visible: path.length > 0
                    width: parent.width
                    height: visible ? 28 : 0
                    spacing: 8

                    ThemedIcon {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        source: Qt.resolvedUrl("../resources/Icons/TablerFileFilled.svg")
                        color: Theme.textMuted
                    }

                    Text {
                        Layout.fillWidth: true
                        text: panel.pathName(recentPathRow.path)
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                width: parent.width
                height: panel.visibleRecentPaths.length === 0 ? 28 : 0
                visible: height > 0
                text: panel.status
                color: Theme.textMuted
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }
}
