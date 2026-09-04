pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Rectangle {
    id: list

    required property ApplicationListModel applications
    required property string selectedApplicationId
    property bool scanning: false

    signal applicationSelected(string applicationId)
    signal applicationsRequested()

    implicitHeight: 53 + Math.min(5, applications.count) * 46
    radius: Theme.radiusMedium
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Column {
        anchors.fill: parent

        RowLayout {
            width: parent.width
            height: 52
            spacing: 10

            Text {
                Layout.leftMargin: 16
                Layout.fillWidth: true
                text: "应用占用"
                color: Theme.textPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Text {
                text: "按占用排序"
                color: Theme.textMuted
                font.pixelSize: 11
            }

            IconButton {
                Layout.rightMargin: 10
                iconSource: Qt.resolvedUrl("../resources/Icons/TablerArrowUpRight.svg")
                tooltip: "打开应用列表"
                onClicked: list.applicationsRequested()
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        ListView {
            id: applicationList

            width: parent.width
            height: Math.min(5, list.applications.count) * 46
            model: list.applications
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: -1
            keyNavigationEnabled: false

            delegate: ApplicationRow {
                required property int index
                required property string appId
                required property string appName
                required property string shortName
                required property url iconSource
                required property string location
                required property string category
                required property string sizeText
                required property string fileCount
                required property string modified
                required property string riskText
                required property int riskLevel
                required property int accentIndex

                width: parent.width
                rowData: ({
                    "appId": appId,
                    "appName": appName,
                    "shortName": shortName,
                    "iconSource": iconSource,
                    "location": location,
                    "category": category,
                    "sizeText": sizeText,
                    "fileCount": fileCount,
                    "modified": modified,
                    "riskText": riskText,
                    "riskLevel": riskLevel,
                    "accentIndex": accentIndex
                })
                selected: !list.scanning && list.selectedApplicationId === appId
                onActivated: applicationId => list.applicationSelected(applicationId)
            }
        }
    }
}
