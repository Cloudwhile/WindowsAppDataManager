import QtQuick
import QtQuick.Controls

Rectangle {
    id: panel

    required property var rowData

    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: detailContent.implicitHeight + 36
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        ApplicationDetailContent {
            id: detailContent
            x: 18
            y: 18
            width: parent.width - 36
            application: panel.rowData
            paneMode: true
        }
    }
}
