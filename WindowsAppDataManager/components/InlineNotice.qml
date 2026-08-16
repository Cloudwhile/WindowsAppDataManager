import QtQuick
import QtQuick.Layouts

Rectangle {
    id: notice

    required property url iconSource
    required property string message
    property color accent: Theme.amberText
    property color fill: Theme.amberSoft
    property url actionIconSource
    property string actionTooltip: ""
    property bool actionVisible: false

    signal actionRequested()

    implicitHeight: 44
    radius: Theme.radiusSmall
    color: fill

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 9

        ThemedIcon {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            source: notice.iconSource
            color: notice.accent
        }

        Text {
            Layout.fillWidth: true
            text: notice.message
            color: notice.accent
            font.pixelSize: 11
            font.weight: Font.Medium
            elide: Text.ElideMiddle
        }

        IconButton {
            visible: notice.actionVisible
            iconSource: notice.actionIconSource
            symbolColor: notice.accent
            tooltip: notice.actionTooltip
            onClicked: notice.actionRequested()
        }
    }
}
