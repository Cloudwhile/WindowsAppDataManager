import QtQuick

Rectangle {
    id: sidebar

    required property int currentPage
    required property int overviewPageIndex
    required property int applicationsPageIndex
    required property int applicationDetailPageIndex
    required property int cleanupPageIndex
    required property int settingsPageIndex

    property int applicationCount: AppStore.applications.count
    property string appDataSizeText: AppStore.applications.totalSizeText
    property string totalFileCountText: AppStore.applications.totalFileCountText
    property string reclaimableSizeText: AppStore.applications.reclaimableSizeText
    property real reclaimableRatio: AppStore.applications.reclaimableRatio

    signal pageRequested(int pageIndex)

    implicitWidth: 232
    color: Theme.sidebar

    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 18
        spacing: 4

        Text {
            leftPadding: 12
            bottomPadding: 3
            text: "工作区"
            color: Theme.textMuted
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 0.5
        }

        NavItem {
            width: parent.width
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerHome.svg")
            selectedIconSource: Qt.resolvedUrl("../resources/Icons/TablerHomeFilled.svg")
            label: "扫描概览"
            selected: sidebar.currentPage === sidebar.overviewPageIndex
            onClicked: sidebar.pageRequested(sidebar.overviewPageIndex)
        }

        Item { width: 1; height: 12 }

        Text {
            leftPadding: 12
            bottomPadding: 3
            text: "数据管理"
            color: Theme.textMuted
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 0.5
        }

        NavItem {
            width: parent.width
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerFiles.svg")
            selectedIconSource: Qt.resolvedUrl("../resources/Icons/TablerFilesFilled.svg")
            label: "应用管理"
            selected: sidebar.currentPage === sidebar.applicationsPageIndex
                      || sidebar.currentPage === sidebar.applicationDetailPageIndex
            onClicked: sidebar.pageRequested(sidebar.applicationsPageIndex)
        }

        NavItem {
            width: parent.width
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerTrash.svg")
            selectedIconSource: Qt.resolvedUrl("../resources/Icons/TablerTrashFilled.svg")
            label: "清理建议"
            selected: sidebar.currentPage === sidebar.cleanupPageIndex
            onClicked: sidebar.pageRequested(sidebar.cleanupPageIndex)
        }
    }

    Item {
        id: footer

        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        height: 174

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.divider
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 14
            text: "APPDATA 摘要"
            color: Theme.textMuted
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0.6
        }

        ThemedIcon {
            id: summaryIcon

            width: 20
            height: 20
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 40
            source: Qt.resolvedUrl("../resources/Icons/TablerFolderFilled.svg")
            color: Theme.amber
        }

        Text {
            anchors.left: summaryIcon.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: summaryIcon.verticalCenter
            text: sidebar.appDataSizeText
            color: Theme.textPrimary
            font.pixelSize: 14
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.top: summaryIcon.bottom
            anchors.topMargin: 8
            text: sidebar.applicationCount + " 个应用 · "
                  + sidebar.totalFileCountText + " 个文件"
            color: Theme.textSecondary
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.bottom: reclaimTrack.top
            anchors.bottomMargin: 5
            text: "可重新生成"
            color: Theme.textMuted
            font.pixelSize: 10
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: reclaimTrack.top
            anchors.bottomMargin: 5
            text: sidebar.reclaimableSizeText
            color: Theme.greenText
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }

        Rectangle {
            id: reclaimTrack

            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: settingsItem.top
            anchors.bottomMargin: 12
            height: 4
            radius: 2
            color: Theme.divider

            Rectangle {
                width: parent.width * Math.max(0, Math.min(100,
                                                          sidebar.reclaimableRatio)) / 100
                height: parent.height
                radius: parent.radius
                color: Theme.green

                Behavior on width {
                    NumberAnimation {
                        duration: Motion.normal
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        NavItem {
            id: settingsItem

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerSettings.svg")
            selectedIconSource: Qt.resolvedUrl("../resources/Icons/TablerSettingsFilled.svg")
            label: "设置"
            selected: sidebar.currentPage === sidebar.settingsPageIndex
            onClicked: sidebar.pageRequested(sidebar.settingsPageIndex)
        }
    }
}
