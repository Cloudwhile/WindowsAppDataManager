pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page

    property string searchText: ""
    property int riskFilterIndex: 0
    property bool sortDescending: true
    property string scanStatus: "尚未扫描"
    property string lastScanText: "尚未扫描"
    signal applicationSelected(int index)

    readonly property int acceptedRiskLevel: riskFilterIndex - 1
    readonly property int visibleApplicationCount: {
        const revision = AppStore.applications.revision
        let count = 0
        for (let index = 0; index < AppStore.applications.count; ++index) {
            if (matchesApplication(AppStore.applications.get(index)))
                ++count
        }
        return count
    }

    function matchesApplication(application) {
        const query = searchText.trim().toLowerCase()
        const matchesText = query.length === 0
                || application.appName.toLowerCase().includes(query)
                || application.publisher.toLowerCase().includes(query)
                || application.category.toLowerCase().includes(query)
        const matchesRisk = acceptedRiskLevel < 0 || application.riskLevel === acceptedRiskLevel
        return matchesText && matchesRisk
    }

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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 9

                TextField {
                    id: searchField

                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    placeholderText: "搜索应用、发布者或分类"
                    text: page.searchText
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    selectByMouse: true
                    leftPadding: 34
                    rightPadding: 10
                    font.pixelSize: 12
                    onTextChanged: page.searchText = text

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: searchField.activeFocus ? Theme.accent : Theme.border
                    }

                    ThemedIcon {
                        width: 17
                        height: 17
                        anchors.left: parent.left
                        anchors.leftMargin: 11
                        anchors.verticalCenter: parent.verticalCenter
                        source: Qt.resolvedUrl("../resources/Icons/TablerZoom.svg")
                        color: Theme.textMuted
                    }
                }

                ComboBox {
                    id: riskFilter

                    Layout.preferredWidth: page.width < 660 ? 112 : 132
                    Layout.preferredHeight: 36
                    model: ["全部风险", "安全", "低风险", "需确认", "高风险", "受保护", "未知"]
                    currentIndex: page.riskFilterIndex
                    font.pixelSize: 11
                    onActivated: index => page.riskFilterIndex = index

                    contentItem: Text {
                        leftPadding: 10
                        rightPadding: 25
                        text: riskFilter.displayText
                        color: Theme.textSecondary
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: riskFilter.activeFocus ? Theme.accent : Theme.border
                    }
                }

                IconButton {
                    iconSource: page.sortDescending
                                ? Qt.resolvedUrl("../resources/Icons/TablerArrowNarrowDown.svg")
                                : Qt.resolvedUrl("../resources/Icons/TablerArrowNarrowUp.svg")
                    tooltip: page.sortDescending ? "当前按占用从大到小" : "当前按占用从小到大"
                    onClicked: page.sortDescending = !page.sortDescending
                }
            }
        }

        Row {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            spacing: 1

            Repeater {
                model: [
                    { "label": "当前结果", "value": page.visibleApplicationCount.toString(), "detail": "共 " + AppStore.applications.count + " 个应用归属", "color": Theme.accent },
                    { "label": "合计占用", "value": AppStore.applications.totalSizeText, "detail": "按应用聚合", "color": Theme.purple },
                    { "label": "可重新生成", "value": AppStore.applications.reclaimableSizeText, "detail": "仍需逐项验证", "color": Theme.green }
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
                        text: page.sortDescending ? "占用：从大到小" : "占用：从小到大"
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
                    model: AppStore.applications.count
                    keyNavigationEnabled: true
                    ScrollBar.vertical: ScrollBar { }

                    delegate: ApplicationRow {
                        id: applicationDelegate

                        required property int index
                        readonly property int sourceIndex: page.sortDescending
                                                           ? index
                                                           : AppStore.applications.count - index - 1
                        width: ListView.view.width
                        rowIndex: sourceIndex
                        rowData: {
                            const revision = AppStore.applications.revision
                            return AppStore.applications.get(sourceIndex)
                        }
                        selected: AppStore.currentIndex === sourceIndex
                        filterText: page.searchText
                        acceptedRiskLevel: page.acceptedRiskLevel
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
