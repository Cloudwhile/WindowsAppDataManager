pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page

    property bool scanning: false
    property real scanProgress: 0
    property string currentPath: ""
    property var recentPaths: []
    property string scanStatus: "尚未扫描"
    property string lastScanText: "尚未扫描"
    property bool scanFailed: false
    property bool partialResult: false
    property int issueCount: 0

    signal scanRequested()
    signal applicationSelected(int index)
    signal applicationsRequested()

    readonly property bool hasResults: AppStore.applications.count > 0
    readonly property bool showScanWorkspace: scanning || !hasResults
    readonly property bool showResults: hasResults
    readonly property string pageTitle: scanning ? "正在扫描"
                                                : hasResults ? "AppData 概览"
                                                             : "扫描概览"
    readonly property string pageSubtitle: scanning
                                           ? (currentPath.length > 0
                                              ? currentPath : scanStatus)
                                           : scanFailed
                                             ? "本次扫描失败，保留上一次完整结果"
                                           : partialResult ? scanStatus
                                           : lastScanText === "尚未扫描"
                                             ? scanStatus
                                             : "最近扫描于" + lastScanText

    contentWidth: width
    contentHeight: contentColumn.implicitHeight + 36
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { }

    Column {
        id: contentColumn

        x: 18
        y: 18
        width: parent.width - 36
        spacing: 14

        RowLayout {
            width: parent.width
            spacing: 12

            Column {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    width: parent.width
                    text: page.pageTitle
                    color: Theme.textPrimary
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: page.pageSubtitle
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }
            }

            IconButton {
                iconSource: page.scanning
                            ? Qt.resolvedUrl("../resources/Icons/TablerCancel.svg")
                            : Qt.resolvedUrl("../resources/Icons/TablerPlayerPlayFilled.svg")
                tooltip: page.scanning ? "停止扫描" : "开始扫描"
                prominent: true
                onClicked: page.scanRequested()
            }
        }

        ScanWorkspace {
            id: scanWorkspace

            width: parent.width
            height: page.showScanWorkspace ? scanWorkspace.implicitHeight : 0
            opacity: page.showScanWorkspace ? 1 : 0
            visible: scanWorkspace.height > 0 || scanWorkspace.opacity > 0
            clip: true
            progress: page.scanProgress
            currentPath: page.currentPath
            status: page.scanStatus
            applications: AppStore.applications
            recentPaths: page.recentPaths
            issueCount: page.issueCount
            active: page.scanning
            onDetailsRequested: page.applicationsRequested()

            Behavior on height {
                NumberAnimation {
                    duration: Motion.allowPosition ? Motion.normal : 0
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on opacity {
                NumberAnimation { duration: Motion.fast }
            }
        }

        InlineNotice {
            id: scanNotice

            width: parent.width
            readonly property bool shown: !page.scanning && page.showResults
                                          && (page.scanFailed || page.partialResult)
            height: scanNotice.shown ? scanNotice.implicitHeight : 0
            opacity: scanNotice.shown ? 1 : 0
            visible: scanNotice.height > 0 || scanNotice.opacity > 0
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
            message: page.scanFailed
                     ? "本次扫描未完成，当前仍显示上一次完整结果。"
                     : "扫描完成，但有 " + page.issueCount + " 个位置未能完整读取。"
            accent: page.scanFailed ? Theme.redText : Theme.amberText
            fill: page.scanFailed ? Theme.redSoft : Theme.amberSoft
            actionIconSource: Qt.resolvedUrl("../resources/Icons/TablerRefresh.svg")
            actionTooltip: "重新扫描"
            actionVisible: true
            onActionRequested: page.scanRequested()

            Behavior on height {
                NumberAnimation {
                    duration: Motion.allowPosition ? Motion.normal : 0
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on opacity {
                NumberAnimation { duration: Motion.fast }
            }
        }

        OverviewStats {
            id: overviewStats

            width: parent.width
            height: page.showResults ? overviewStats.implicitHeight : 0
            opacity: page.showResults ? 1 : 0
            visible: overviewStats.height > 0 || overviewStats.opacity > 0
            clip: true
            applications: AppStore.applications

            Behavior on height {
                NumberAnimation {
                    duration: Motion.allowPosition ? Motion.normal : 0
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on opacity {
                NumberAnimation { duration: Motion.fast }
            }
        }

        DispositionSummary {
            id: dispositionSummary

            width: parent.width
            height: page.showResults ? dispositionSummary.implicitHeight : 0
            opacity: page.showResults ? 1 : 0
            visible: dispositionSummary.height > 0 || dispositionSummary.opacity > 0
            clip: true
            applications: AppStore.applications
            active: page.scanning

            Behavior on height {
                NumberAnimation {
                    duration: Motion.allowPosition ? Motion.normal : 0
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on opacity {
                NumberAnimation { duration: Motion.fast }
            }
        }

        OverviewApplicationList {
            id: overviewApplicationList

            width: parent.width
            height: page.showResults ? overviewApplicationList.implicitHeight : 0
            opacity: page.showResults ? 1 : 0
            visible: overviewApplicationList.height > 0
                     || overviewApplicationList.opacity > 0
            clip: true
            applications: AppStore.applications
            selectedIndex: AppStore.currentIndex
            onApplicationSelected: index => page.applicationSelected(index)
            onApplicationsRequested: page.applicationsRequested()

            Behavior on height {
                NumberAnimation {
                    duration: Motion.allowPosition ? Motion.normal : 0
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on opacity {
                NumberAnimation { duration: Motion.fast }
            }
        }
    }
}
