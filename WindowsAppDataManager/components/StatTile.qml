import QtQuick
import QtQuick.Layouts

Item {
    id: tile

    required property url iconSource
    property string label: ""
    property string value: ""
    property string detail: ""
    property color accent: Theme.accent

    implicitHeight: 92

    RowLayout {
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 13

        Rectangle {
            Layout.preferredWidth: 38
            Layout.preferredHeight: 38
            radius: Theme.radiusMedium
            color: Qt.rgba(tile.accent.r, tile.accent.g, tile.accent.b, Theme.dark ? 0.2 : 0.12)

            ThemedIcon {
                width: 20
                height: 20
                anchors.centerIn: parent
                source: tile.iconSource
                color: tile.accent
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: tile.label
                color: Theme.textSecondary
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: tile.value
                color: tile.accent
                font.pixelSize: 23
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: tile.detail
                color: Theme.textMuted
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }
}
