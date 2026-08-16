import QtQuick
import QtQuick.Layouts

Rectangle {
    id: bar

    required property url statusIcon
    required property color statusColor
    required property string statusText
    required property string totalSizeText
    required property bool scanning
    required property real scanProgress
    required property int issueCount
    required property bool completed
    required property bool detailVisible
    required property string selectedApplicationName

    implicitHeight: 28
    color: Theme.surface

    readonly property string trailingStatus: scanning
                                                     ? Math.round(scanProgress) + "%"
                                                     : issueCount > 0
                                                       ? issueCount + " 个位置需注意"
                                                       : completed ? "就绪" : ""

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

        ThemedIcon {
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            source: bar.statusIcon
            color: bar.statusColor
        }

        Text {
            Layout.fillWidth: true
            text: bar.statusText
            color: bar.statusColor
            font.pixelSize: 10
            elide: Text.ElideMiddle
        }

        Text {
            text: bar.totalSizeText
            color: Theme.textSecondary
            font.pixelSize: 10
        }

        Text {
            visible: bar.detailVisible && bar.selectedApplicationName.length > 0
            text: "当前：" + bar.selectedApplicationName
            color: Theme.textMuted
            font.pixelSize: 10
            elide: Text.ElideRight
            Layout.maximumWidth: 220
        }

        Text {
            text: bar.trailingStatus
            color: bar.issueCount > 0 && !bar.scanning ? Theme.amberText : Theme.textMuted
            font.pixelSize: 10
        }
    }
}
