import QtQuick

Item {
    id: state

    required property url iconSource
    required property string title
    required property string description
    property url actionIconSource
    property string actionTooltip: ""
    property color accent: Theme.accent
    property bool actionVisible: false

    signal actionRequested()

    implicitHeight: 240

    Column {
        width: Math.min(420, Math.max(240, state.width - 48))
        anchors.centerIn: parent
        spacing: 8

        ThemedIcon {
            width: 34
            height: 34
            anchors.horizontalCenter: parent.horizontalCenter
            source: state.iconSource
            color: state.accent
        }

        Text {
            width: parent.width
            text: state.title
            color: Theme.textPrimary
            font.pixelSize: 15
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: state.description
            visible: text.length > 0
            color: Theme.textSecondary
            font.pixelSize: 11
            lineHeight: 1.35
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        IconButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: state.actionVisible
            iconSource: state.actionIconSource
            tooltip: state.actionTooltip
            prominent: true
            onClicked: state.actionRequested()
        }
    }
}
