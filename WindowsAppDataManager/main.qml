import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import windowsappdatamanager

ApplicationWindow {
    id: window

    visible: true
    width: Math.min(1280, Screen.desktopAvailableWidth - 48)
    height: Math.min(800, Screen.desktopAvailableHeight - 72)
    minimumWidth: 1024
    minimumHeight: 640
    title: "Windows AppData 管理器"
    color: Theme.canvas

    readonly property ScanViewModel scanController: Backend.scan
    readonly property SettingsViewModel settingsController: Backend.settings
    readonly property ApplicationFilterModel applicationFilter: Backend.applicationFilter
    readonly property int overviewPageIndex: 0
    readonly property int applicationsPageIndex: 1
    readonly property int applicationDetailPageIndex: 2
    readonly property int settingsPageIndex: 3
    property int currentPage: overviewPageIndex
    readonly property bool scanning: scanController.running
    readonly property real scanProgress: scanController.progress
    readonly property bool detailSupported: currentPage === overviewPageIndex
                                            || currentPage === applicationsPageIndex
    readonly property bool showDetails: width >= 1180 && detailSupported
    readonly property int detailPanelWidth: width >= 1360 ? 384 : width >= 1240 ? 360 : 332
    readonly property int sidebarWidth: width < 1120 ? 184 : 208
    readonly property string appDataPath: scanController.targetPath
    readonly property color scanStatusColor: scanning ? Theme.accent
                                                      : scanController.errorMessage.length > 0 ? Theme.red
                                                      : scanController.partialResult ? Theme.amber
                                                      : scanController.progress === 100 ? Theme.green
                                                                                       : Theme.textMuted
    readonly property url scanStatusIcon: scanning
                                           ? Qt.resolvedUrl("resources/Icons/TablerActivityHeartbeat.svg")
                                           : scanController.errorMessage.length > 0
                                             ? Qt.resolvedUrl("resources/Icons/TablerExclamationMark.svg")
                                           : scanController.partialResult
                                             ? Qt.resolvedUrl("resources/Icons/TablerExclamationMark.svg")
                                           : scanController.progress === 100
                                             ? Qt.resolvedUrl("resources/Icons/TablerCheck.svg")
                                             : Qt.resolvedUrl("resources/Icons/TablerPointFilled.svg")

    function toggleScan() {
        scanController.toggleScan()
    }

    function cycleThemeMode() {
        settingsController.themeMode = (settingsController.themeMode + 1) % 3
    }

    function openApplication(index) {
        AppStore.selectApplication(index)
        if (width < 1180)
            currentPage = applicationDetailPageIndex
    }

    function returnToApplications() {
        currentPage = applicationsPageIndex
    }

    onWidthChanged: {
        if (width >= 1180 && currentPage === applicationDetailPageIndex)
            currentPage = applicationsPageIndex
    }

    Component.onCompleted: {
        Theme.mode = settingsController.themeMode
        Motion.preference = settingsController.motionPreference
    }

    Connections {
        target: window.settingsController

        function onThemeModeChanged() {
            Theme.mode = window.settingsController.themeMode
        }

        function onMotionPreferenceChanged() {
            Motion.preference = window.settingsController.motionPreference
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: window.currentPage === window.applicationDetailPageIndex
        onActivated: window.returnToApplications()
    }

    Rectangle {
        id: commandBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 58
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 14
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 8
                color: Theme.accent

                ThemedIcon {
                    width: 18
                    height: 18
                    anchors.centerIn: parent
                    source: Qt.resolvedUrl("resources/Icons/TablerChartPieFilled.svg")
                    color: "#ffffff"
                }
            }

            ColumnLayout {
                Layout.preferredWidth: window.sidebarWidth - 62
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: "AppData 管理器"
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    text: window.scanning ? "扫描进行中" : "本机"
                    color: window.scanning ? Theme.accent : Theme.textMuted
                    font.pixelSize: 10
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 28
                color: Theme.divider
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 570
                spacing: 8

                Text {
                    visible: window.width >= 1080
                    text: "目标"
                    color: Theme.textMuted
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    border.width: 1
                    border.color: Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        ThemedIcon {
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            source: Qt.resolvedUrl("resources/Icons/TablerFolderFilled.svg")
                            color: Theme.amber
                        }

                        Text {
                            Layout.fillWidth: true
                            text: window.appDataPath
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            IconButton {
                iconSource: window.scanning
                            ? Qt.resolvedUrl("resources/Icons/TablerCancel.svg")
                            : Qt.resolvedUrl("resources/Icons/TablerPlayerPlayFilled.svg")
                tooltip: window.scanning ? "停止扫描" : "开始扫描"
                prominent: true
                onClicked: window.toggleScan()
            }

            IconButton {
                iconSource: Qt.resolvedUrl("resources/Icons/TablerRefresh.svg")
                tooltip: "刷新结果"
                enabled: !window.scanning
                onClicked: window.scanController.startScan()
            }

            IconButton {
                iconSource: Theme.dark
                            ? Qt.resolvedUrl("resources/Icons/TablerMoonFilled.svg")
                            : Qt.resolvedUrl("resources/Icons/TablerSunFilled.svg")
                tooltip: "主题：" + Theme.modeName
                onClicked: window.cycleThemeMode()
            }

            IconButton {
                iconSource: Qt.resolvedUrl("resources/Icons/TablerSettings.svg")
                tooltip: "设置"
                onClicked: window.currentPage = window.settingsPageIndex
            }
        }
    }

    Rectangle {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 28
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12

            RowLayout {
                spacing: 5

                ThemedIcon {
                    Layout.preferredWidth: 12
                    Layout.preferredHeight: 12
                    source: window.scanStatusIcon
                    color: window.scanStatusColor
                }

                Text {
                    text: window.scanController.statusText
                    color: window.scanStatusColor
                    font.pixelSize: 10
                }
            }

            Text {
                text: AppStore.applications.totalSizeText
                color: Theme.textSecondary
                font.pixelSize: 10
            }

            Item { Layout.fillWidth: true }

            Text {
                visible: window.showDetails && AppStore.applications.count > 0
                text: "当前：" + AppStore.selectedApplication.appName
                color: Theme.textMuted
                font.pixelSize: 10
            }

            Text {
                text: "100%"
                color: Theme.textMuted
                font.pixelSize: 10
            }
        }
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: commandBar.bottom
        anchors.bottom: footer.top
        spacing: 0

        Rectangle {
            Layout.preferredWidth: window.sidebarWidth
            Layout.fillHeight: true
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
                    iconSource: Qt.resolvedUrl("resources/Icons/TablerHome.svg")
                    selectedIconSource: Qt.resolvedUrl("resources/Icons/TablerHomeFilled.svg")
                    label: "概览"
                    selected: window.currentPage === window.overviewPageIndex
                    onClicked: window.currentPage = window.overviewPageIndex
                }

                NavItem {
                    width: parent.width
                    iconSource: Qt.resolvedUrl("resources/Icons/TablerFiles.svg")
                    selectedIconSource: Qt.resolvedUrl("resources/Icons/TablerFilesFilled.svg")
                    label: "应用"
                    selected: window.currentPage === window.applicationsPageIndex
                              || window.currentPage === window.applicationDetailPageIndex
                    onClicked: window.currentPage = window.applicationsPageIndex
                }

                Item { width: 1; height: 8 }

                Text {
                    leftPadding: 10
                    text: "管理"
                    color: Theme.textMuted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }

                NavItem {
                    width: parent.width
                    iconSource: Qt.resolvedUrl("resources/Icons/TablerFolder.svg")
                    selectedIconSource: Qt.resolvedUrl("resources/Icons/TablerFolderFilled.svg")
                    label: "潜在残留"
                    badge: AppStore.applications.potentialOrphanCount > 0
                           ? AppStore.applications.potentialOrphanCount.toString() : ""
                    enabled: false
                }

                NavItem {
                    width: parent.width
                    iconSource: Qt.resolvedUrl("resources/Icons/IcBaselineCleaningServices.svg")
                    label: "清理计划"
                    enabled: false
                }
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                spacing: 3

                NavItem {
                    width: parent.width
                    iconSource: Qt.resolvedUrl("resources/Icons/TablerSettings.svg")
                    selectedIconSource: Qt.resolvedUrl("resources/Icons/TablerSettingsFilled.svg")
                    label: "设置"
                    selected: window.currentPage === window.settingsPageIndex
                    onClicked: window.currentPage = window.settingsPageIndex
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            currentIndex: window.currentPage

            OverviewPage {
                scanning: window.scanning
                scanProgress: window.scanProgress
                currentPath: window.scanController.currentPath
                scanStatus: window.scanController.statusText
                lastScanText: window.scanController.lastScanText
                onScanRequested: window.toggleScan()
                onApplicationSelected: index => window.openApplication(index)
                onApplicationsRequested: window.currentPage = window.applicationsPageIndex
            }

            ApplicationsPage {
                filterModel: window.applicationFilter
                scanStatus: window.scanController.statusText
                lastScanText: window.scanController.lastScanText
                onApplicationSelected: index => window.openApplication(index)
            }

            ApplicationDetailPage {
                application: AppStore.selectedApplication
                onBackRequested: window.returnToApplications()
            }

            SettingsPage {
                settingsController: window.settingsController
            }
        }

        DetailPanel {
            Layout.preferredWidth: window.showDetails ? window.detailPanelWidth : 0
            Layout.minimumWidth: window.showDetails ? window.detailPanelWidth : 0
            Layout.maximumWidth: window.showDetails ? window.detailPanelWidth : 0
            Layout.fillHeight: true
            visible: window.showDetails
            rowData: AppStore.selectedApplication

            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic }
            }
        }
    }
}
