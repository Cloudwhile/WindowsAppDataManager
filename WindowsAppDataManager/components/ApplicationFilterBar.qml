import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: control

    required property ApplicationFilterModel filterModel

    readonly property bool compact: width < 760
    readonly property string sortLabel: sortMode.currentIndex === 0 ? "占用"
                                                : sortMode.currentIndex === 1 ? "名称"
                                                                              : "风险"

    implicitHeight: compact ? 98 : 54
    radius: Theme.radiusMedium
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    GridLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 9
        anchors.bottomMargin: 9
        columns: control.compact ? 4 : 5
        columnSpacing: 8
        rowSpacing: 8

        TextField {
            id: searchField

            Layout.fillWidth: true
            Layout.columnSpan: control.compact ? 4 : 1
            Layout.minimumWidth: 180
            Layout.preferredHeight: 34
            placeholderText: "搜索应用、发布者或分类"
            text: control.filterModel.searchText
            color: Theme.textPrimary
            placeholderTextColor: Theme.textMuted
            selectByMouse: true
            Accessible.name: "搜索应用"
            leftPadding: 34
            rightPadding: 10
            font.pixelSize: 12
            onTextChanged: control.filterModel.searchText = text

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

        FilterComboBox {
            id: installStateFilter

            Layout.fillWidth: control.compact
            Layout.preferredWidth: control.compact ? 132 : 116
            Layout.preferredHeight: 34
            Accessible.name: "安装状态筛选"
            model: ["全部状态", "已安装", "潜在残留", "状态未知"]
            currentIndex: control.filterModel.installStateFilter === 0 ? 1
                          : control.filterModel.installStateFilter === 1 ? 2
                          : control.filterModel.installStateFilter === 2 ? 3 : 0
            onActivated: index => control.filterModel.installStateFilter = index === 1 ? 0
                                                                                       : index === 2 ? 1
                                                                                       : index === 3 ? 2 : -1
        }

        FilterComboBox {
            id: riskFilter

            Layout.fillWidth: control.compact
            Layout.preferredWidth: control.compact ? 124 : 112
            Layout.preferredHeight: 34
            Accessible.name: "风险筛选"
            model: ["全部风险", "安全", "低风险", "需确认", "高风险", "受保护", "未知"]
            currentIndex: control.filterModel.riskFilter + 1
            onActivated: index => control.filterModel.riskFilter = index - 1
        }

        FilterComboBox {
            id: sortMode

            Layout.fillWidth: control.compact
            Layout.preferredWidth: control.compact ? 116 : 100
            Layout.preferredHeight: 34
            Accessible.name: "排序方式"
            model: ["按占用", "按名称", "按风险"]
            currentIndex: control.filterModel.sortMode
            onActivated: index => control.filterModel.sortMode = index
        }

        IconButton {
            iconSource: control.filterModel.sortDescending
                        ? Qt.resolvedUrl("../resources/Icons/TablerArrowNarrowDown.svg")
                        : Qt.resolvedUrl("../resources/Icons/TablerArrowNarrowUp.svg")
            tooltip: control.sortLabel + (control.filterModel.sortDescending
                                           ? "：降序" : "：升序")
            onClicked: control.filterModel.sortDescending = !control.filterModel.sortDescending
        }
    }
}
