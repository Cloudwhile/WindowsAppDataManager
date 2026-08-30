pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: list

    required property ApplicationListModel applications
    required property int selectedIndex

    signal applicationSelected(int index)
    signal applicationsRequested()

    implicitHeight: 312
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
                onClicked: list.applicationsRequested()
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        Repeater {
            model: Math.min(5, list.applications.count)

            ApplicationRow {
                required property int index
                width: parent.width
                rowIndex: index
                rowData: {
                    const revision = list.applications.revision
                    return list.applications.getSummary(index)
                }
                selected: list.selectedIndex === index
                onActivated: selectedIndex => list.applicationSelected(selectedIndex)
            }
        }
    }
}
