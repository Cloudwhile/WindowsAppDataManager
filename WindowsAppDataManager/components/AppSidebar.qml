import QtQuick

Rectangle {
    id: sidebar

    required property int currentPage
    required property int overviewPageIndex
    required property int applicationsPageIndex
    required property int applicationDetailPageIndex
    required property int cleanupPageIndex
    required property int settingsPageIndex

    signal pageRequested(int pageIndex)

    color: Theme.surface

    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.top: parent.top
        anchors.topMargin: 17
        spacing: 5

        Text {
            leftPadding: 10
            text: "分析"
            color: Theme.textMuted
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }

        NavItem {
            width: parent.width
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerHome.svg")
            selectedIconSource: Qt.resolvedUrl("../resources/Icons/TablerHomeFilled.svg")
            label: "概览"
            selected: sidebar.currentPage === sidebar.overviewPageIndex
            onClicked: sidebar.pageRequested(sidebar.overviewPageIndex)
        }

        NavItem {
            width: parent.width
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerFiles.svg")
            selectedIconSource: Qt.resolvedUrl("../resources/Icons/TablerFilesFilled.svg")
            label: "应用"
            selected: sidebar.currentPage === sidebar.applicationsPageIndex
                      || sidebar.currentPage === sidebar.applicationDetailPageIndex
            onClicked: sidebar.pageRequested(sidebar.applicationsPageIndex)
        }

        NavItem {
            width: parent.width
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerTrash.svg")
            selectedIconSource: Qt.resolvedUrl("../resources/Icons/TablerTrashFilled.svg")
            label: "清理"
            selected: sidebar.currentPage === sidebar.cleanupPageIndex
            onClicked: sidebar.pageRequested(sidebar.cleanupPageIndex)
        }

    }

    NavItem {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        iconSource: Qt.resolvedUrl("../resources/Icons/TablerSettings.svg")
        selectedIconSource: Qt.resolvedUrl("../resources/Icons/TablerSettingsFilled.svg")
        label: "设置"
        selected: sidebar.currentPage === sidebar.settingsPageIndex
        onClicked: sidebar.pageRequested(sidebar.settingsPageIndex)
    }
}
