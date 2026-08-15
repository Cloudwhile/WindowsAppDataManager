import QtQuick

Item {
    id: metric

    property string label: ""
    property string value: ""
    property color accent: Theme.textPrimary

    implicitHeight: 48

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            width: parent.width
            text: metric.label
            color: Theme.textMuted
            font.pixelSize: 10
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: metric.value
            color: metric.accent
            font.pixelSize: 17
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }
}
