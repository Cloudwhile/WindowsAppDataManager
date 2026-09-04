import QtQuick

Item {
    id: control

    required property url iconSource
    required property string shortName
    required property int accentIndex

    implicitWidth: 26
    implicitHeight: 26

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Theme.applicationAccent(control.accentIndex)

        Image {
            id: iconImage
            anchors.fill: parent
            anchors.margins: 2
            source: control.iconSource
            sourceSize.width: 32
            sourceSize.height: 32
            fillMode: Image.PreserveAspectFit
            visible: status === Image.Ready
        }

        Text {
            anchors.centerIn: parent
            text: control.shortName
            color: Theme.applicationAccentText(control.accentIndex)
            font.pixelSize: control.shortName.length > 2 ? 8 : 11
            font.weight: Font.Bold
            visible: !iconImage.visible
        }
    }
}
