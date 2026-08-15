pragma Singleton

import QtQuick

QtObject {
    id: store

    property int currentIndex: 0
    readonly property ApplicationListModel applications: Backend.applications
    readonly property int dataRevision: applications.revision
    readonly property var selectedApplication: {
        const revision = dataRevision
        if (applications.count > 0)
            return applications.get(Math.min(currentIndex, applications.count - 1))
        return emptyApplication()
    }

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
            "accent": "#7f8a96",
            "summary": "开始扫描后，这里会显示应用归属、数据分类与识别证据。",
            "dataGroups": [],
            "evidence": []
        }
    }

    function selectApplication(index) {
        if (index >= 0 && index < applications.count)
            currentIndex = index
    }

    property Connections modelConnections: Connections {
        target: store.applications

        function onCountChanged() {
            if (store.applications.count === 0)
                store.currentIndex = 0
            else if (store.currentIndex >= store.applications.count)
                store.currentIndex = store.applications.count - 1
        }
    }
}
