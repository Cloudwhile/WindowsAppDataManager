import QtQuick
import QtQuick.Controls

Rectangle {
    id: panel

    required property var rowData
    signal closeRequested()

    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Item {
        id: panelHeader

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 42

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: "详细信息"
            color: Theme.textSecondary
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }

        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerX.svg")
            tooltip: "关闭详细信息"
            onClicked: panel.closeRequested()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: panelHeader.bottom
        height: 1
        color: Theme.divider
    }

    Flickable {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: panelHeader.bottom
        anchors.topMargin: 1
        anchors.bottom: parent.bottom
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
