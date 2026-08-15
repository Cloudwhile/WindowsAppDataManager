import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page

    required property var application
    signal backRequested()

    contentWidth: width
    contentHeight: contentColumn.implicitHeight + 44
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { }

    Column {
        id: contentColumn
        x: 24
        y: 18
        width: parent.width - 48
        spacing: 16

        RowLayout {
            width: parent.width
            spacing: 8

            IconButton {
                iconSource: Qt.resolvedUrl("../resources/Icons/TablerChevronLeft.svg")
                tooltip: "返回应用列表"
                onClicked: page.backRequested()
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    Layout.fillWidth: true
                    text: "应用详情"
                    color: Theme.textPrimary
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }

                Text {
                    Layout.fillWidth: true
                    text: page.application.fileCount + " 个文件 · 最近修改 " + page.application.modified
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
            }
        }

        ApplicationDetailContent {
            id: detailContent
            width: parent.width
            application: page.application
        }
    }
}
