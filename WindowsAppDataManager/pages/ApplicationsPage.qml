pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page

    required property ApplicationFilterModel filterModel
    property string scanStatus: "尚未扫描"
    property string lastScanText: "尚未扫描"
    signal applicationSelected(int index)

    readonly property int visibleApplicationCount: filterModel.count
    readonly property string sortLabel: filterModel.sortMode === 0 ? "占用"
                                                : filterModel.sortMode === 1 ? "名称"
                                                                              : "风险"

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: 20
        anchors.bottomMargin: 20
        spacing: 14

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: "应用"
                color: Theme.textPrimary
                font.pixelSize: 25
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: AppStore.applications.count + " 个应用归属 · "
                      + (page.lastScanText === "尚未扫描"
                         ? page.scanStatus
                         : "最近扫描于" + page.lastScanText)
                color: Theme.textSecondary
                font.pixelSize: 12
            }
        }

        ApplicationFilterBar {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            filterModel: page.filterModel
        }

        Row {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            spacing: 1

            Repeater {
                model: [
                    { "label": "当前结果", "value": page.visibleApplicationCount.toString(), "detail": "共 " + AppStore.applications.count + " 个应用归属", "color": Theme.accent },
                    { "label": "全部占用", "value": AppStore.applications.totalSizeText, "detail": "全部应用聚合", "color": Theme.purple },
                    { "label": "可重新生成", "value": AppStore.applications.reclaimableSizeText, "detail": "全部结果，仍需逐项验证", "color": Theme.green }
                ]

                delegate: Rectangle {
                    id: summaryTile

                    required property var modelData
                    width: (parent.width - 2) / 3
                    height: parent.height
                    radius: Theme.radiusMedium
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Text { width: parent.width; text: summaryTile.modelData.label; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                        Text { width: parent.width; text: summaryTile.modelData.value; color: summaryTile.modelData.color; font.pixelSize: 19; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text { width: parent.width; text: summaryTile.modelData.detail; color: Theme.textSecondary; font.pixelSize: 10; elide: Text.ElideRight }
                    }
                }
            }
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

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    spacing: 10

                    Text {
                        Layout.leftMargin: 14
                        Layout.fillWidth: true
                        text: page.visibleApplicationCount + " 个结果"
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: page.sortLabel + (page.filterModel.sortDescending
                                                ? "：降序" : "：升序")
                        color: Theme.textMuted
                        font.pixelSize: 10
                    }

                    Item { Layout.preferredWidth: 14 }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

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
                        onActivated: selectedIndex => page.applicationSelected(selectedIndex)
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                visible: page.visibleApplicationCount === 0
                spacing: 5

                ThemedIcon {
                    width: 24
                    height: 24
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: Qt.resolvedUrl("../resources/Icons/TablerZoom.svg")
                    color: Theme.textMuted
                }

                Text {
                    text: "没有符合条件的应用"
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
            }
        }
    }
}
