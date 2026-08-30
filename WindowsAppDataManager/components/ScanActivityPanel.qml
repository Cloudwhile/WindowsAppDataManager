pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel

    required property real progress
    required property string currentPath
    required property string status
    required property var recentPaths
    required property bool active

    readonly property real normalizedProgress: Math.max(0, Math.min(100, progress))
    readonly property int maximumVisibleRows: 3
    readonly property var visiblePaths: {
        const paths = []
        if (currentPath.length > 0)
            paths.push(currentPath)

        for (let index = 0; index < recentPaths.length && paths.length < 5; ++index) {
            const path = recentPaths[index]
            if (path && paths.indexOf(path) < 0)
                paths.push(path)
        }
        return paths
    }
    readonly property real activityContentHeight: Math.max(64, visiblePaths.length * 52)
    readonly property real activityViewportHeight: Math.min(activityContentHeight,
                                                              maximumVisibleRows * 52)

    function pathName(path) {
        const normalized = path.replace(/[\\/]+$/, "")
        const parts = normalized.split(/[\\/]/)
        return parts.length > 0 && parts[parts.length - 1].length > 0
                ? parts[parts.length - 1] : path
    }

    implicitHeight: 48 + activityViewportHeight
    radius: Theme.radiusLarge
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Item {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 46

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: "扫描活动"
            color: Theme.textPrimary
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: panel.active ? "实时更新"
                               : panel.normalizedProgress >= 100 ? "已完成" : "待命"
            color: panel.active ? Theme.accent : Theme.textMuted
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        height: 1
        color: Theme.divider
    }

    Flickable {
        id: activityViewport

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.topMargin: 1
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        clip: true
        contentWidth: width
        contentHeight: activityContent.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        pixelAligned: true

        ScrollBar.vertical: ScrollBar {
            policy: activityViewport.contentHeight > activityViewport.height
                    ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: activityContent

            width: activityViewport.width

            Repeater {
                model: 5

                delegate: Item {
                    id: activityRow

                    required property int index
                    readonly property string path: index < panel.visiblePaths.length
                                                   ? panel.visiblePaths[index] : ""
                    readonly property bool current: panel.active
                                                    && path === panel.currentPath

                    visible: path.length > 0
                    width: parent.width
                    height: visible ? 52 : 0

                    Rectangle {
                        anchors.fill: parent
                        color: activityRow.current ? Theme.surfaceSelected : "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        spacing: 11

                        ThemedIcon {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            source: activityRow.current
                                    ? Qt.resolvedUrl("../resources/Icons/TablerActivityHeartbeat.svg")
                                    : Qt.resolvedUrl("../resources/Icons/TablerCheck.svg")
                            color: activityRow.current ? Theme.accent : Theme.green
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                width: parent.width
                                text: panel.pathName(activityRow.path)
                                color: Theme.textPrimary
                                font.pixelSize: 12
                                font.weight: activityRow.current ? Font.DemiBold : Font.Medium
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: activityRow.path
                                color: Theme.textMuted
                                font.pixelSize: 10
                                elide: Text.ElideMiddle
                            }
                        }

                        Text {
                            text: activityRow.current ? "当前" : "已扫描"
                            color: activityRow.current ? Theme.accent : Theme.greenText
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        visible: activityRow.path.length > 0
                                 && activityRow.index < panel.visiblePaths.length - 1
                        anchors.left: parent.left
                        anchors.leftMargin: 47
                        anchors.right: parent.right
                        anchors.rightMargin: 18
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.divider
                    }
                }
            }

            Item {
                width: parent.width
                height: panel.visiblePaths.length === 0 ? 64 : 0
                visible: height > 0

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 10

                    ThemedIcon {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        source: panel.active
                                ? Qt.resolvedUrl("../resources/Icons/TablerActivityHeartbeat.svg")
                                : Qt.resolvedUrl("../resources/Icons/TablerPointFilled.svg")
                        color: panel.active ? Theme.accent : Theme.textMuted
                    }

                    Text {
                        Layout.fillWidth: true
                        text: panel.status
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
