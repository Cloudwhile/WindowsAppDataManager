import QtQuick
import QtQuick.Layouts

Rectangle {
    id: strip

    required property bool scanning
    required property real progress
    required property string currentPath
    required property string statusText

    implicitHeight: scanning ? 48 : 0
    opacity: scanning ? 1 : 0
    visible: opacity > 0
    radius: Theme.radiusMedium
    color: Theme.accentSoft

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 12

        Text {
            Layout.preferredWidth: Math.min(210, implicitWidth)
            text: strip.currentPath.length > 0 ? strip.currentPath : strip.statusText
            color: Theme.accentText
            font.pixelSize: 12
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 6
            radius: 3
            color: Theme.scanTrack

            Rectangle {
                width: parent.width * Math.max(0, Math.min(100, strip.progress)) / 100
                height: parent.height
                radius: parent.radius
                color: Theme.accent

                Behavior on width {
                    NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic }
                }
            }
        }

        Text {
            text: Math.round(strip.progress) + "%"
            color: Theme.accentText
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }

    Behavior on implicitHeight {
        NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic }
    }

    Behavior on opacity {
        NumberAnimation { duration: Motion.fast }
    }
}
