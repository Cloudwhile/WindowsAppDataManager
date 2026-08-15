pragma ComponentBehavior: Bound

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
    signal scanRequested()
    signal applicationSelected(int index)
    signal applicationsRequested()

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
        }

        Rectangle {
            width: parent.width
            height: page.scanning ? 48 : 0
            opacity: page.scanning ? 1 : 0
            visible: opacity > 0
            radius: Theme.radiusMedium
            color: Theme.accentSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12

                Text {
                    Layout.preferredWidth: Math.min(210, implicitWidth)
                    text: page.currentPath.length > 0 ? page.currentPath : page.scanStatus
                    color: Theme.accent
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    radius: 3
                    color: Theme.dark ? "#294d72" : "#bed8f7"

                    Rectangle {
                        width: parent.width * page.scanProgress / 100
                        height: parent.height
                        radius: parent.radius
                        color: Theme.accent

                        Behavior on width {
                            NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
                        }
                    }
                }

                Text {
                    text: Math.round(page.scanProgress) + "%"
                    color: Theme.accent
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }

            Behavior on height {
                NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic }
            }
            Behavior on opacity {
                NumberAnimation { duration: Motion.fast }
            }
        }

        Rectangle {
            width: parent.width
            height: page.width < 900 ? 164 : 94
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            Grid {
                id: statsGrid
                anchors.fill: parent
                columns: page.width < 900 ? 2 : 4
                rows: columns === 2 ? 2 : 1

                Repeater {
                    model: [
                        { "icon": Qt.resolvedUrl("../resources/Icons/TablerFileFilled.svg"), "label": "AppData 占用", "value": AppStore.applications.totalSizeText, "detail": "共 " + AppStore.applications.totalFileCountText + " 个文件", "accent": Theme.accent },
                        { "icon": Qt.resolvedUrl("../resources/Icons/IcBaselineCleaningServices.svg"), "label": "可重新生成", "value": AppStore.applications.reclaimableSizeText, "detail": "仍需逐项验证", "accent": Theme.green },
                        { "icon": Qt.resolvedUrl("../resources/Icons/TablerFilesFilled.svg"), "label": "应用归属", "value": AppStore.applications.count.toString(), "detail": AppStore.applications.recognizedCount + " 个已识别", "accent": Theme.purple },
                        { "icon": Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg"), "label": "潜在残留", "value": AppStore.applications.potentialOrphanCount.toString(), "detail": "仅显示证据充分的候选", "accent": Theme.amber }
                    ]

                    delegate: Item {
                        id: statDelegate

                        required property int index
                        required property var modelData
                        width: statsGrid.width / statsGrid.columns
                        height: statsGrid.height / statsGrid.rows

                        StatTile {
                            anchors.fill: parent
                            iconSource: statDelegate.modelData.icon
                            label: statDelegate.modelData.label
                            value: statDelegate.modelData.value
                            detail: statDelegate.modelData.detail
                            accent: statDelegate.modelData.accent
                        }

                        Rectangle {
                            visible: statDelegate.index % statsGrid.columns > 0
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 1
                            height: 54
                            color: Theme.divider
                        }

                        Rectangle {
                            visible: statsGrid.rows > 1 && statDelegate.index >= statsGrid.columns
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 1
                            color: Theme.divider
                        }
                    }
                }
            }
        }

        Row {
            width: parent.width
            height: 264
            spacing: 16

            Rectangle {
                width: Math.max(302, (parent.width - parent.spacing) * 0.43)
                height: parent.height
                radius: Theme.radiusMedium
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.top: parent.top
                    anchors.topMargin: 16
                    text: "数据判定分布"
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.top: parent.top
                    anchors.topMargin: 58
                    spacing: 12

                    StorageRing {
                        width: Math.min(154, parent.width * 0.49)
                        height: width
                        localValue: AppStore.applications.reclaimableRatio
                        roamingValue: AppStore.applications.protectedRatio
                        lowValue: AppStore.applications.reviewRatio
                        totalText: AppStore.applications.totalSizeText
                        totalLabel: "已分析"
                    }

                    Column {
                        width: parent.width - 170
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 15

                        Repeater {
                            model: [
                                { "label": "可重新生成", "value": AppStore.applications.reclaimableSizeText, "ratio": AppStore.applications.reclaimableRatio.toFixed(1) + "%", "color": Theme.accent },
                                { "label": "受保护", "value": AppStore.applications.protectedSizeText, "ratio": AppStore.applications.protectedRatio.toFixed(1) + "%", "color": Theme.green },
                                { "label": "需确认 / 未知", "value": AppStore.applications.reviewSizeText, "ratio": AppStore.applications.reviewRatio.toFixed(1) + "%", "color": Theme.amber }
                            ]

                            delegate: Row {
                                id: storageLegendRow

                                required property var modelData
                                width: parent.width
                                spacing: 8

                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: storageLegendRow.modelData.color
                                }

                                Column {
                                    width: parent.width - 16
                                    spacing: 1
                                    Text { text: storageLegendRow.modelData.label + "  " + storageLegendRow.modelData.ratio; color: Theme.textPrimary; font.pixelSize: 12; font.weight: Font.Medium }
                                    Text { text: storageLegendRow.modelData.value; color: Theme.textMuted; font.pixelSize: 11 }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width - siblingWidth - parent.spacing
                height: parent.height
                radius: Theme.radiusMedium
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                property real siblingWidth: Math.max(302, (parent.width - parent.spacing) * 0.43)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.top: parent.top
                    anchors.topMargin: 16
                    text: "占用最多的应用"
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.top: parent.top
                    anchors.topMargin: 52
                    spacing: 13

                    Repeater {
                        model: Math.min(5, AppStore.applications.count)

                        delegate: Column {
                            id: usageBar

                            required property int index
                            readonly property var app: {
                                const revision = AppStore.applications.revision
                                return AppStore.applications.get(index)
                            }
                            width: parent.width
                            spacing: 5

                            Row {
                                width: parent.width
                                Text { width: parent.width - 74; text: usageBar.app.appName; color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideRight }
                                Text { width: 74; text: usageBar.app.sizeText; color: Theme.textPrimary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                            }

                            Rectangle {
                                width: parent.width
                                height: 7
                                radius: 3
                                color: Theme.divider

                                Rectangle {
                                    width: parent.width * usageBar.app.sizeValue
                                           / AppStore.applications.maximumSizeValue
                                    height: parent.height
                                    radius: parent.radius
                                    color: usageBar.app.accent
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 312
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            Column {
                anchors.fill: parent

                RowLayout {
                    width: parent.width
                    height: 52
                    spacing: 10

                    Text {
                        Layout.leftMargin: 16
                        Layout.fillWidth: true
                        text: "应用占用"
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: "按占用排序"
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }

                    IconButton {
                        Layout.rightMargin: 10
                        iconSource: Qt.resolvedUrl("../resources/Icons/TablerArrowUpRight.svg")
                        tooltip: "打开应用列表"
                        onClicked: page.applicationsRequested()
                    }
                }

                Rectangle { width: parent.width; height: 1; color: Theme.divider }

                Repeater {
                    model: Math.min(5, AppStore.applications.count)

                    ApplicationRow {
                        required property int index
                        width: parent.width
                        rowIndex: index
                        rowData: {
                            const revision = AppStore.applications.revision
                            return AppStore.applications.get(index)
                        }
                        selected: AppStore.currentIndex === index
                        onActivated: selectedIndex => page.applicationSelected(selectedIndex)
                    }
                }
            }
        }
    }
}
