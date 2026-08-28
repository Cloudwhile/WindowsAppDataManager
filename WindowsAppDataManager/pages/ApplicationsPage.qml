pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page

    required property ApplicationFilterModel filterModel
    property bool scanning: false
    property string scanStatus: "尚未扫描"
    property string lastScanText: "尚未扫描"
    property bool scanFailed: false
    property bool partialResult: false
    signal scanRequested()
    signal applicationSelected(int index)

    readonly property int visibleApplicationCount: filterModel.count
    readonly property bool hasApplications: AppStore.applications.count > 0
    readonly property bool hasActiveFilters: filterModel.searchText.trim().length > 0
                                             || filterModel.riskFilter >= 0
                                             || filterModel.installStateFilter >= 0
    readonly property string sortLabel: filterModel.sortMode === 0 ? "占用"
                                                : filterModel.sortMode === 1 ? "名称"
                                                                              : "风险"

    function clearFilters() {
        filterModel.searchText = ""
        filterModel.riskFilter = -1
        filterModel.installStateFilter = -1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 18
        anchors.bottomMargin: 18
        spacing: 12

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: "应用管理"
                color: Theme.textPrimary
                font.pixelSize: 25
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: AppStore.applications.count + " 个应用归属 · "
                      + (page.scanning ? page.scanStatus
                         : page.scanFailed ? "本次扫描失败，当前显示上一次完整结果"
                         : page.partialResult ? page.scanStatus
                         : page.lastScanText === "尚未扫描" ? page.scanStatus
                                                            : "最近扫描于" + page.lastScanText)
                color: Theme.textSecondary
                font.pixelSize: 12
            }
        }

        ApplicationFilterBar {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: page.hasApplications
            filterModel: page.filterModel
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 190
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                ApplicationListHeader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.hasApplications ? implicitHeight : 0
                    visible: page.hasApplications
                    resultCount: page.visibleApplicationCount
                    totalCount: AppStore.applications.count
                    totalSizeText: AppStore.applications.totalSizeText
                    reclaimableSizeText: AppStore.applications.reclaimableSizeText
                    sortText: page.sortLabel + (page.filterModel.sortDescending
                                                ? "：降序" : "：升序")
                }

                ListView {
                    id: applicationList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: page.filterModel
                    keyNavigationEnabled: true
                    ScrollBar.vertical: ScrollBar { }

                    delegate: ApplicationRow {
                        id: applicationDelegate

                        required property int index
                        width: ListView.view.width
                        rowData: page.filterModel.get(index)
                        rowIndex: rowData.sourceIndex
                        selected: AppStore.currentIndex === rowIndex
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                applicationList.currentIndex = applicationDelegate.index
                                applicationList.positionViewAtIndex(applicationDelegate.index,
                                                                    ListView.Contain)
                            }
                        }
                        onActivated: selectedIndex => page.applicationSelected(selectedIndex)
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: page.visibleApplicationCount === 0
                width: Math.min(parent.width - 40, 520)
                height: Math.min(parent.height - 20, 260)
                iconSource: page.scanning && !page.hasApplications
                            ? Qt.resolvedUrl("../resources/Icons/TablerActivityHeartbeat.svg")
                            : page.hasApplications
                              ? Qt.resolvedUrl("../resources/Icons/TablerZoom.svg")
                              : Qt.resolvedUrl("../resources/Icons/TablerZoomScan.svg")
                title: page.scanning && !page.hasApplications ? "正在分析应用归属"
                                                              : page.hasApplications
                                                                ? "没有符合条件的应用"
                                                                : "尚无应用数据"
                description: page.scanning && !page.hasApplications ? page.scanStatus : ""
                actionIconSource: page.hasApplications
                                  ? Qt.resolvedUrl("../resources/Icons/TablerX.svg")
                                  : Qt.resolvedUrl("../resources/Icons/TablerPlayerPlayFilled.svg")
                actionTooltip: page.hasApplications ? "清除筛选" : "开始扫描"
                actionVisible: !page.scanning && (!page.hasApplications || page.hasActiveFilters)
                onActionRequested: {
                    if (page.hasApplications)
                        page.clearFilters()
                    else
                        page.scanRequested()
                }
            }
        }
    }
}
