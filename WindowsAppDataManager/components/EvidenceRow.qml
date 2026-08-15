import QtQuick
import QtQuick.Layouts

Item {
    id: evidence

    required property var rowData

    implicitHeight: 46

    readonly property color statusColor: rowData.status === 0 ? Theme.green
                                                 : rowData.status === 1 ? Theme.amber
                                                 : rowData.status === 3 ? Theme.red
                                                                       : Theme.neutral
    readonly property url statusIconSource: rowData.status === 0
                                                ? Qt.resolvedUrl("../resources/Icons/TablerCheck.svg")
                                                : rowData.status === 1
                                                  ? Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
                                                : rowData.status === 3
                                                  ? Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
                                                  : Qt.resolvedUrl("../resources/Icons/TablerQuestionMark.svg")

    RowLayout {
        anchors.fill: parent
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            radius: 10
            color: Qt.rgba(evidence.statusColor.r, evidence.statusColor.g,
                           evidence.statusColor.b, Theme.dark ? 0.2 : 0.12)

            ThemedIcon {
                width: 12
                height: 12
                anchors.centerIn: parent
                source: evidence.statusIconSource
                color: evidence.statusColor
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: evidence.rowData.sourceText
                    color: Theme.textPrimary
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                Text {
                    text: evidence.rowData.statusText
                    color: evidence.statusColor
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }

            Text {
                Layout.fillWidth: true
                text: evidence.rowData.detail
                color: Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }
}
