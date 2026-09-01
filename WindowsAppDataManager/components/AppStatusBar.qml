import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar

    required property url statusIcon
    required property color statusColor
    required property string statusText
    required property string totalSizeText
    required property bool scanning
    required property real scanProgress
    required property int issueCount
    required property bool completed
    required property bool detailVisible
    required property string selectedApplicationName

    property string currentPath: Backend.scan.currentPath.length > 0
                                 ? Backend.scan.currentPath : Backend.scan.targetPath
    property string pathLabel: "当前路径"

    implicitHeight: 38
    color: Theme.statusBar

    readonly property string trailingStatus: scanning
                                                     ? "扫描中"
                                                     : issueCount > 0
                                                       ? issueCount + " 个位置需注意"
                                                       : completed ? "就绪" : ""

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 10

        ThemedIcon {
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            source: bar.statusIcon
            color: bar.statusColor
        }

        Flickable {
            id: statusFlickable

            Layout.preferredWidth: 210
            Layout.maximumWidth: 210
            Layout.minimumWidth: 96
            Layout.preferredHeight: 20
            Layout.alignment: Qt.AlignVCenter
            contentWidth: statusLabel.implicitWidth
            contentHeight: height
            readonly property real maximumContentX:
                Math.max(0, contentWidth - width)
            clip: true
            pixelAligned: true
            interactive: contentWidth > width
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            onMaximumContentXChanged:
                contentX = Math.min(contentX, maximumContentX)

            Text {
                id: statusLabel

                anchors.verticalCenter: parent.verticalCenter
                text: "扫描状态：" + bar.statusText
                color: bar.statusColor
                font.pixelSize: 11
                wrapMode: Text.NoWrap
                elide: Text.ElideNone
            }

            WheelHandler {
                enabled: statusFlickable.interactive
                acceptedDevices: PointerDevice.Mouse
                                 | PointerDevice.TouchPad
                target: null

                onWheel: event => {
                    const pixel = Math.abs(event.pixelDelta.x)
                                  >= Math.abs(event.pixelDelta.y)
                                  ? event.pixelDelta.x : event.pixelDelta.y
                    const angle = Math.abs(event.angleDelta.x)
                                  >= Math.abs(event.angleDelta.y)
                                  ? event.angleDelta.x : event.angleDelta.y
                    const delta = pixel !== 0 ? pixel : angle / 3
                    const next = Math.max(0, Math.min(
                                              statusFlickable.maximumContentX,
                                              statusFlickable.contentX - delta))
                    event.accepted = next !== statusFlickable.contentX
                    statusFlickable.contentX = next
                }
            }

            ScrollBar.horizontal: ScrollBar {
                height: 2
                padding: 0
                policy: statusFlickable.interactive
                        ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

                contentItem: Rectangle {
                    implicitWidth: 24
                    implicitHeight: 2
                    radius: 1
                    color: bar.statusColor
                    opacity: 0.85
                }

                background: Rectangle {
                    radius: 1
                    color: Theme.divider
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 16
            color: Theme.divider
        }

        ThemedIcon {
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            source: Qt.resolvedUrl("../resources/Icons/TablerFolder.svg")
            color: Theme.textMuted
        }

        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            text: bar.pathLabel + "：" + (bar.currentPath.length > 0 ? bar.currentPath : "—")
            color: Theme.textSecondary
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }

        Text {
            visible: bar.detailVisible && bar.selectedApplicationName.length > 0
            text: "当前应用：" + bar.selectedApplicationName
            color: Theme.textMuted
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.maximumWidth: 180
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 16
            color: Theme.divider
        }

        Text {
            text: "AppData " + bar.totalSizeText
            color: Theme.textSecondary
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }

        Text {
            visible: bar.trailingStatus.length > 0
            text: bar.trailingStatus
            color: bar.issueCount > 0 && !bar.scanning ? Theme.amberText : Theme.textMuted
            font.pixelSize: 11
            font.weight: bar.scanning ? Font.DemiBold : Font.Normal
        }
    }
}
