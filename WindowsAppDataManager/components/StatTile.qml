import QtQuick

Item {
    id: tile

    required property url iconSource
    property string label: ""
    property string value: ""
    property string detail: ""
    property color accent: Theme.accent

    implicitHeight: 92

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 13

        Rectangle {
            width: 38
            height: 38
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

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                text: tile.label
                color: Theme.textSecondary
                font.pixelSize: 12
            }

            Text {
                text: tile.value
                color: tile.accent
                font.pixelSize: 23
                font.weight: Font.DemiBold
            }

            Text {
                text: tile.detail
                color: Theme.textMuted
                font.pixelSize: 11
            }
        }
    }
}
