pragma Singleton

import QtQuick

QtObject {
    id: store

    property string currentApplicationId: ""
    readonly property ApplicationListModel applications: Backend.applications
    readonly property bool scanning: Backend.scan.running
    readonly property var emptyApplicationData: emptyApplication()
    readonly property int currentIndex: applications.revision >= 0
                                        && currentApplicationId.length > 0
                                        ? applications.indexOfId(currentApplicationId)
                                        : -1
    readonly property var selectedApplication: !scanning
                                               && applications.revision >= 0
                                               && currentIndex >= 0
                                               ? applications.get(currentIndex)
                                               : emptyApplicationData

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

    function selectApplicationById(applicationId) {
        if (applicationId.length === 0 || applications.indexOfId(applicationId) < 0)
            return false

        currentApplicationId = applicationId
        return true
    }

    function selectApplication(index) {
        if (index < 0 || index >= applications.count)
            return false
        return selectApplicationById(applications.getSummary(index).appId)
    }

    function restoreSelection() {
        if (applications.count === 0) {
            currentApplicationId = ""
            return
        }

        if (applications.indexOfId(currentApplicationId) >= 0)
            return

        currentApplicationId = applications.getSummary(0).appId
    }

    onScanningChanged: {
        if (!scanning)
            restoreSelection()
    }

    Component.onCompleted: store.restoreSelection()

    property Connections modelConnections: Connections {
        target: store.applications

        function onCountChanged() {
            if (!store.scanning)
                store.restoreSelection()
        }

        function onRevisionChanged() {
            if (!store.scanning)
                store.restoreSelection()
        }
    }
}
