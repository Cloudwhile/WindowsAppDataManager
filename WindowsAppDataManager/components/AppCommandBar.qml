import QtQuick
import QtQuick.Layouts

Rectangle {
    id: bar

    required property bool scanning
    required property bool scanEnabled
    required property bool cleanupRunning
    required property string targetPath
    required property int sidebarWidth
    required property bool darkTheme
    required property string themeName

    signal scanToggled()
    signal themeRequested()
    signal settingsRequested()

    implicitHeight: 58
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 14
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            radius: Theme.radiusMedium
            color: Theme.accent

            ThemedIcon {
                width: 18
                height: 18
                anchors.centerIn: parent
                source: Qt.resolvedUrl("../resources/Icons/TablerChartPieFilled.svg")
                color: Theme.onAccent
            }
        }

        ColumnLayout {
            Layout.preferredWidth: bar.sidebarWidth - 62
            spacing: 0

            Text {
                Layout.fillWidth: true
                text: "AppData 管理器"
                color: Theme.textPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                text: bar.scanning ? "扫描进行中"
                                   : bar.cleanupRunning ? "清理进行中" : "本机"
                color: bar.scanning || bar.cleanupRunning
                       ? Theme.accentText : Theme.textMuted
                font.pixelSize: 10
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 28
            color: Theme.divider
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 570
            spacing: 8

            Text {
                visible: bar.width >= 1080
                text: "目标"
                color: Theme.textMuted
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                Layout.preferredHeight: 34
                radius: Theme.radiusSmall
                color: Theme.surfaceRaised
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    ThemedIcon {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        source: Qt.resolvedUrl("../resources/Icons/TablerFolderFilled.svg")
                        color: Theme.amber
                    }

                    Text {
                        Layout.fillWidth: true
                        text: bar.targetPath
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        IconButton {
            iconSource: bar.scanning
                        ? Qt.resolvedUrl("../resources/Icons/TablerCancel.svg")
                        : Qt.resolvedUrl("../resources/Icons/TablerPlayerPlayFilled.svg")
            tooltip: bar.scanning ? "停止扫描" : "开始扫描"
            prominent: true
            enabled: bar.scanning || bar.scanEnabled
            onClicked: bar.scanToggled()
        }

        IconButton {
            iconSource: bar.darkTheme
                        ? Qt.resolvedUrl("../resources/Icons/TablerMoonFilled.svg")
                        : Qt.resolvedUrl("../resources/Icons/TablerSunFilled.svg")
            tooltip: "主题：" + bar.themeName
            onClicked: bar.themeRequested()
        }

        IconButton {
            iconSource: Qt.resolvedUrl("../resources/Icons/TablerSettings.svg")
            tooltip: "设置"
            onClicked: bar.settingsRequested()
        }
    }
}
