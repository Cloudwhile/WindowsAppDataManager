import QtQuick
import QtQuick.Controls
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
    property string detailActionText: ""
    property bool detailActionVisible: false

    signal actionRequested()
    signal detailActionRequested()

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

        Button {
            id: detailAction

            visible: notice.detailActionVisible
            text: notice.detailActionText
            hoverEnabled: true
            padding: 0
            font.pixelSize: 11
            font.weight: Font.DemiBold

            contentItem: Text {
                text: detailAction.text
                color: notice.accent
                font.pixelSize: 11
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                radius: Theme.radiusSmall
                color: detailAction.down ? Theme.surfaceSelected
                     : detailAction.hovered ? Theme.surfaceHover : "transparent"
            }

            onClicked: notice.detailActionRequested()
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
