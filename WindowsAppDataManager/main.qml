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
    palette.window: Theme.surface
    palette.windowText: Theme.textPrimary
    palette.base: Theme.surfaceRaised
    palette.alternateBase: Theme.surface
    palette.text: Theme.textPrimary
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    palette.placeholderText: Theme.textMuted
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.textPrimary

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
    readonly property bool wideDetailMode: width >= 1420
    readonly property bool selectedApplicationVisible: currentPage !== applicationsPageIndex
                                                        || applicationFilter.containsSourceIndex(
                                                            AppStore.currentIndex)
    readonly property bool showDetails: wideDetailMode && detailSupported
                                        && AppStore.applications.count > 0 && detailPaneOpen
                                        && selectedApplicationVisible
    readonly property int detailPanelWidth: width >= 1520 ? 384 : 360
    readonly property int sidebarWidth: width < 1120 ? 184 : 208
    property real detailPanelExtent: showDetails ? detailPanelWidth : 0
    property bool detailPaneOpen: false

    onCurrentPageChanged: pageFade.restart()
    readonly property color scanStatusColor: scanning ? Theme.accentText
                                                      : scanController.errorMessage.length > 0 ? Theme.redText
                                                      : scanController.partialResult ? Theme.amberText
                                                      : scanController.progress === 100 ? Theme.greenText
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
        if (wideDetailMode)
            detailPaneOpen = true
        else
            currentPage = applicationDetailPageIndex
    }

    function returnToApplications() {
        currentPage = applicationsPageIndex
    }

    onWidthChanged: {
        if (wideDetailMode && currentPage === applicationDetailPageIndex) {
            detailPaneOpen = true
            currentPage = applicationsPageIndex
        }
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

    AppCommandBar {
        id: commandBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        scanning: window.scanning
        targetPath: window.scanController.targetPath
        sidebarWidth: window.sidebarWidth
        darkTheme: Theme.dark
        themeName: Theme.modeName
        onScanToggled: window.toggleScan()
        onThemeRequested: window.cycleThemeMode()
        onSettingsRequested: window.currentPage = window.settingsPageIndex
    }

    AppStatusBar {
        id: statusBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        statusIcon: window.scanStatusIcon
        statusColor: window.scanStatusColor
        statusText: window.scanController.statusText
        totalSizeText: AppStore.applications.totalSizeText
        scanning: window.scanning
        scanProgress: window.scanProgress
        issueCount: window.scanController.issueCount
        completed: window.scanController.progress === 100
                   && window.scanController.errorMessage.length === 0
        detailVisible: window.showDetails
        selectedApplicationName: AppStore.applications.count > 0
                                 ? AppStore.selectedApplication.appName : ""
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: commandBar.bottom
        anchors.bottom: statusBar.top
        spacing: 0

        AppSidebar {
            Layout.preferredWidth: window.sidebarWidth
            Layout.fillHeight: true
            currentPage: window.currentPage
            overviewPageIndex: window.overviewPageIndex
            applicationsPageIndex: window.applicationsPageIndex
            applicationDetailPageIndex: window.applicationDetailPageIndex
            settingsPageIndex: window.settingsPageIndex
            onPageRequested: pageIndex => window.currentPage = pageIndex
        }

        StackLayout {
            id: contentStack

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
                scanFailed: window.scanController.errorMessage.length > 0
                partialResult: window.scanController.partialResult
                issueCount: window.scanController.issueCount
                onScanRequested: window.toggleScan()
                onApplicationSelected: index => window.openApplication(index)
                onApplicationsRequested: window.currentPage = window.applicationsPageIndex
            }

            ApplicationsPage {
                filterModel: window.applicationFilter
                scanning: window.scanning
                scanStatus: window.scanController.statusText
                lastScanText: window.scanController.lastScanText
                scanFailed: window.scanController.errorMessage.length > 0
                partialResult: window.scanController.partialResult
                onScanRequested: window.toggleScan()
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
            Layout.preferredWidth: window.detailPanelExtent
            Layout.minimumWidth: window.detailPanelExtent
            Layout.maximumWidth: window.detailPanelExtent
            Layout.fillHeight: true
            visible: window.detailPanelExtent > 0
            rowData: AppStore.selectedApplication
            onCloseRequested: window.detailPaneOpen = false
        }
    }

    Behavior on detailPanelExtent {
        NumberAnimation {
            duration: Motion.allowPosition ? Motion.normal : 0
            easing.type: Easing.OutCubic
        }
    }

    NumberAnimation {
        id: pageFade
        target: contentStack
        property: "opacity"
        from: Motion.preference === 2 ? 1 : 0.72
        to: 1
        duration: Motion.normal
        easing.type: Easing.OutCubic
    }
}
