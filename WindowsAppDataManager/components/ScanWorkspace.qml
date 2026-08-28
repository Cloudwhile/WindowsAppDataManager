pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Item {
    id: workspace

    required property real progress
    required property string currentPath
    required property string status
    required property ApplicationListModel applications
    required property var recentPaths
    required property int issueCount
    required property bool active

    signal detailsRequested()

    // 双列时左侧至少保留 690 px，确保数字进度不会在中间宽度消失。
    readonly property bool compact: width < 1014
    readonly property real primaryHeight: overviewPanel.implicitHeight
                                                + activityPanel.implicitHeight + 14

    implicitHeight: compact
                    ? overviewPanel.implicitHeight + activityPanel.implicitHeight
                      + livePanel.implicitHeight + 28
                    : Math.max(primaryHeight, livePanel.implicitHeight)

    GridLayout {
        anchors.fill: parent
        columns: workspace.compact ? 1 : 2
        columnSpacing: 14
        rowSpacing: 14

        ScanOverviewPanel {
            id: overviewPanel

            Layout.row: 0
            Layout.column: 0
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            progress: workspace.progress
            currentPath: workspace.currentPath
            status: workspace.status
            applications: workspace.applications
            issueCount: workspace.issueCount
            active: workspace.active
        }

        ScanActivityPanel {
            id: activityPanel

            Layout.row: 1
            Layout.column: 0
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            progress: workspace.progress
            currentPath: workspace.currentPath
            status: workspace.status
            recentPaths: workspace.recentPaths
            active: workspace.active
        }

        LiveScanPanel {
            id: livePanel

            Layout.row: workspace.compact ? 2 : 0
            Layout.column: workspace.compact ? 0 : 1
            Layout.rowSpan: workspace.compact ? 1 : 2
            Layout.fillWidth: true
            Layout.fillHeight: !workspace.compact
            Layout.preferredWidth: workspace.compact ? workspace.width : 310
            Layout.maximumWidth: workspace.compact ? workspace.width : 338
            Layout.preferredHeight: workspace.compact ? implicitHeight
                                                      : workspace.primaryHeight
            progress: workspace.progress
            currentPath: workspace.currentPath
            status: workspace.status
            applications: workspace.applications
            recentPaths: workspace.recentPaths
            issueCount: workspace.issueCount
            active: workspace.active
            onDetailsRequested: workspace.detailsRequested()
        }
    }
}
