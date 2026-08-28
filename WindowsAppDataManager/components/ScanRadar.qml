pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Item {
    id: radar

    required property real progress
    required property string status
    required property bool active
    property color accent: Theme.accent
    property color completionColor: Theme.green

    readonly property real normalizedProgress: Math.max(0, Math.min(100, progress))
    readonly property color progressColor: radar.interpolateColor(
                                               radar.accent,
                                               radar.completionColor,
                                               radar.normalizedProgress / 100)

    function interpolateColor(startColor, endColor, amount) {
        const position = Math.max(0, Math.min(1, amount))
        return Qt.rgba(startColor.r + (endColor.r - startColor.r) * position,
                       startColor.g + (endColor.g - startColor.g) * position,
                       startColor.b + (endColor.b - startColor.b) * position,
                       startColor.a + (endColor.a - startColor.a) * position)
    }

    implicitWidth: 196
    implicitHeight: 196

    Accessible.ignored: true

    ColumnLayout {
        width: Math.min(164, radar.width)
        anchors.centerIn: parent
        spacing: 10

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            text: Math.round(radar.normalizedProgress) + "%"
            color: radar.progressColor
            font.pixelSize: 42
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            text: radar.active ? "扫描进度"
                               : radar.normalizedProgress >= 100 ? "扫描完成" : "等待扫描"
            color: Theme.textSecondary
            font.pixelSize: 11
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        ScanProgressBar {
            id: radarProgress

            Layout.fillWidth: true
            Layout.preferredHeight: 7
            progress: radar.normalizedProgress
            active: radar.active
            accent: radar.progressColor
        }

        Text {
            Layout.fillWidth: true
            text: radar.status
            color: Theme.textSecondary
            font.pixelSize: 10
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
}
