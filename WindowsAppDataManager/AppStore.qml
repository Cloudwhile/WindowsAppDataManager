pragma Singleton

import QtQuick

QtObject {
    id: store

    property int currentIndex: 0
    property string currentApplicationId: ""
    readonly property ApplicationListModel applications: Backend.applications
    readonly property bool scanning: Backend.scan.running
    readonly property var emptyApplicationData: emptyApplication()
    property var selectedApplication: emptyApplicationData
    property bool selectionRefreshPending: false

    function emptyApplication() {
        return {
            "appId": "",
            "appName": "尚未选择应用",
            "shortName": "—",
            "publisher": "等待扫描结果",
            "category": "未识别",
            "location": "",
            "executablePath": "",
            "installPath": "",
            "installState": 2,
            "installStateText": "状态未知",
            "confidence": 0,
            "sizeText": "0 B",
            "sizeValue": 0,
            "fileCount": "0",
            "modified": "未知",
            "riskText": "未知",
            "riskLevel": 5,
            "reclaimableText": "0 B",
            "protectedSizeText": "0 B",
            "unknownSizeText": "0 B",
            "accentIndex": 0,
            "summary": "开始扫描后，这里会显示应用归属、数据分类与识别证据。",
            "orphanConfidence": 0,
            "orphanSummary": "",
            "orphanBlockingReasons": [],
            "dataGroups": [],
            "evidence": []
        }
    }

    function selectApplication(index) {
        if (index >= 0 && index < applications.count) {
            currentIndex = index
            currentApplicationId = applications.getSummary(index).appId
            if (!scanning)
                selectedApplication = applications.get(index)
        }
    }

    function restoreSelection() {
        if (applications.count === 0) {
            currentIndex = 0
            currentApplicationId = ""
            return
        }

        const restoredIndex = applications.indexOfId(currentApplicationId)
        if (restoredIndex >= 0) {
            currentIndex = restoredIndex
            return
        }

        currentIndex = Math.min(currentIndex, applications.count - 1)
        currentApplicationId = applications.getSummary(currentIndex).appId
    }

    function refreshSelectedApplication() {
        if (scanning)
            return

        restoreSelection()
        selectedApplication = applications.count > 0
                ? applications.get(Math.min(currentIndex, applications.count - 1))
                : emptyApplicationData
    }

    function scheduleSelectionRefresh() {
        if (scanning || selectionRefreshPending)
            return

        selectionRefreshPending = true
        Qt.callLater(function() {
            store.selectionRefreshPending = false
            store.refreshSelectedApplication()
        })
    }

    onScanningChanged: {
        if (scanning)
            selectedApplication = emptyApplicationData
        else
            refreshSelectedApplication()
    }

    Component.onCompleted: store.refreshSelectedApplication()

    property Connections modelConnections: Connections {
        target: store.applications

        function onCountChanged() {
            store.scheduleSelectionRefresh()
        }

        function onRevisionChanged() {
            store.scheduleSelectionRefresh()
        }
    }
}
