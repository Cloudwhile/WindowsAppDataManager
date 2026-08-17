import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page

    property bool scanning: false
    property real scanProgress: 0
    property string currentPath: ""
    property string scanStatus: "尚未扫描"
    property string lastScanText: "尚未扫描"
    property bool scanFailed: false
    property bool partialResult: false
    property int issueCount: 0
    property string exportMessage: ""
    property bool exportSucceeded: false

    signal scanRequested()
    signal issuesRequested()
    signal exportRequested()
    signal cleanupPlanRequested()
    signal applicationSelected(int index)
    signal applicationsRequested()

    readonly property bool hasResults: AppStore.applications.count > 0

    contentWidth: width
    contentHeight: contentColumn.implicitHeight + 48
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { }

    Column {
        id: contentColumn

        x: 24
        y: 22
        width: parent.width - 48
        spacing: 16

        RowLayout {
            width: parent.width
            spacing: 12

            Column {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: "概览"
                    color: Theme.textPrimary
                    font.pixelSize: 25
                    font.weight: Font.DemiBold
                }

                Text {
                    text: page.scanning ? "正在分析 AppData"
                          : page.scanFailed ? "本次扫描失败，当前显示上一次完整结果"
                          : page.partialResult ? page.scanStatus
                          : page.lastScanText === "尚未扫描" ? page.scanStatus
                                                           : "最近扫描于" + page.lastScanText
                    color: Theme.textSecondary
                    font.pixelSize: 12
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

            IconButton {
                visible: page.hasResults && !page.scanning
                iconSource: Qt.resolvedUrl("../resources/Icons/TablerFileFilled.svg")
                tooltip: "导出扫描报告"
                onClicked: page.exportRequested()
            }

            IconButton {
                visible: page.hasResults && !page.scanning && !page.partialResult
                iconSource: Qt.resolvedUrl("../resources/Icons/IcBaselineCleaningServices.svg")
                tooltip: "生成清理计划"
                onClicked: page.cleanupPlanRequested()
            }
        }

        ScanProgressStrip {
            width: parent.width
            scanning: page.scanning
            progress: page.scanProgress
            currentPath: page.currentPath
            statusText: page.scanStatus
        }

        InlineNotice {
            width: parent.width
            height: implicitHeight
            visible: page.hasResults && !page.scanning
                     && (page.scanFailed || page.partialResult)
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
            message: page.scanFailed
                     ? "本次扫描未完成，当前仍显示上一次完整结果。"
                     : "扫描完成，但有 " + page.issueCount + " 个位置未能完整读取。"
            accent: page.scanFailed ? Theme.redText : Theme.amberText
            fill: page.scanFailed ? Theme.redSoft : Theme.amberSoft
            actionIconSource: Qt.resolvedUrl("../resources/Icons/TablerRefresh.svg")
            actionTooltip: "重新扫描"
            actionVisible: !page.scanning
            detailActionText: "查看明细"
            detailActionVisible: page.partialResult && page.issueCount > 0
            onActionRequested: page.scanRequested()
            onDetailActionRequested: page.issuesRequested()
        }

        InlineNotice {
            width: parent.width
            height: implicitHeight
            visible: page.exportMessage.length > 0
            iconSource: Qt.resolvedUrl(page.exportSucceeded
                                       ? "../resources/Icons/TablerCheck.svg"
                                       : "../resources/Icons/TablerExclamationMark.svg")
            message: page.exportMessage
            accent: page.exportSucceeded ? Theme.greenText : Theme.redText
            fill: page.exportSucceeded ? Theme.greenSoft : Theme.redSoft
        }

        EmptyState {
            width: parent.width
            height: Math.max(280, page.height - 132)
            visible: !page.hasResults
            iconSource: page.scanFailed
                        ? Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
                        : page.scanning
                          ? Qt.resolvedUrl("../resources/Icons/TablerActivityHeartbeat.svg")
                          : Qt.resolvedUrl("../resources/Icons/TablerZoomScan.svg")
            title: page.scanFailed ? "扫描未能完成"
                                   : page.scanning ? "正在分析 AppData"
                                                   : "尚无扫描结果"
            description: page.scanning
                         ? (page.currentPath.length > 0 ? page.currentPath : page.scanStatus)
                         : page.scanFailed ? page.scanStatus
                                           : "开始扫描后，将在这里汇总应用归属、数据判定和占用情况。"
            actionIconSource: Qt.resolvedUrl("../resources/Icons/TablerPlayerPlayFilled.svg")
            actionTooltip: page.scanFailed ? "重新扫描" : "开始扫描"
            actionVisible: !page.scanning
            accent: page.scanFailed ? Theme.red : Theme.accent
            onActionRequested: page.scanRequested()
        }

        OverviewStats {
            width: parent.width
            height: implicitHeight
            visible: page.hasResults
            applications: AppStore.applications
        }

        DispositionSummary {
            width: parent.width
            height: implicitHeight
            visible: page.hasResults
            applications: AppStore.applications
        }

        OverviewApplicationList {
            width: parent.width
            height: implicitHeight
            visible: page.hasResults
            applications: AppStore.applications
            selectedIndex: AppStore.currentIndex
            onApplicationSelected: index => page.applicationSelected(index)
            onApplicationsRequested: page.applicationsRequested()
        }
    }
}
