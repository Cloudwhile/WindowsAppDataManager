pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    property var issues: []
    property bool scanning: false

    signal rescanRequested()

    modal: true
    focus: true
    width: Math.min(720, parent ? parent.width - 56 : 720)
    height: Math.min(600, parent ? parent.height - 72 : 600)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }

    header: Rectangle {
        implicitHeight: 58
        color: Theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 8
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: Theme.radiusSmall
                color: Theme.amberSoft

                ThemedIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
                    color: Theme.amberText
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    text: "未完整读取的位置"
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }

                Text {
                    text: dialog.issues.length + " 个位置需要注意；路径字段可直接选中并复制。"
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
            }

            IconButton {
                visible: !dialog.scanning
                iconSource: Qt.resolvedUrl("../resources/Icons/TablerRefresh.svg")
                tooltip: "重新扫描"
                symbolColor: Theme.amberText
                onClicked: dialog.rescanRequested()
            }

            IconButton {
                iconSource: Qt.resolvedUrl("../resources/Icons/TablerX.svg")
                tooltip: "关闭"
                onClicked: dialog.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

        Text {
            Layout.fillWidth: true
            Layout.margins: 16
            text: "这些位置没有纳入完整统计。请先确认路径和原因；不要因为它们显示为未知就直接处理。"
            color: Theme.textSecondary
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        ListView {
            id: issueList

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 14
            clip: true
            spacing: 8
            model: dialog.issues
            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
                id: issueRow

                required property var modelData
                width: issueList.width
                height: issueContent.implicitHeight + 18
                radius: Theme.radiusMedium
                color: Theme.surfaceRaised
                border.width: 1
                border.color: Theme.border

                Column {
                    id: issueContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        width: parent.width
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: issueRow.modelData.message
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text {
                            text: issueRow.modelData.codeText
                            color: Theme.amberText
                            font.pixelSize: 10
                            font.weight: Font.Medium
                        }
                    }

                    PathField { width: parent.width; label: "位置"; value: issueRow.modelData.path }

                    Text {
                        width: parent.width
                        visible: issueRow.modelData.technicalDetail.length > 0
                        text: "技术详情  " + issueRow.modelData.technicalDetail
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: issueList.count === 0
                text: "当前没有需要展示的位置。"
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }
    }
}
