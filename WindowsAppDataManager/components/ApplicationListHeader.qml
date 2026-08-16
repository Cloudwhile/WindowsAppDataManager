import QtQuick
import QtQuick.Layouts

Item {
    id: header

    required property int resultCount
    required property int totalCount
    required property string totalSizeText
    required property string reclaimableSizeText
    required property string sortText

    implicitHeight: 70

    Column {
        anchors.fill: parent

        RowLayout {
            width: parent.width
            height: 34
            spacing: 10

            Text {
                Layout.leftMargin: 14
                Layout.fillWidth: true
                text: "显示 " + header.resultCount + " / " + header.totalCount
                      + " 个应用  ·  全部占用 " + header.totalSizeText
                      + "  ·  可重新生成 " + header.reclaimableSizeText
                color: Theme.textSecondary
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            Text {
                Layout.rightMargin: 14
                text: header.sortText
                color: Theme.textMuted
                font.pixelSize: 10
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        RowLayout {
            width: parent.width
            height: 34
            spacing: 10

            Item { Layout.leftMargin: 12; Layout.preferredWidth: 28 }

            Text {
                Layout.preferredWidth: 150
                Layout.minimumWidth: 105
                Layout.fillWidth: true
                text: "应用"
                color: Theme.textMuted
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 92
                Layout.maximumWidth: 110
                visible: header.width > 560
                text: "分类"
                color: Theme.textMuted
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            Text {
                Layout.preferredWidth: 70
                text: "占用"
                color: Theme.textMuted
                font.pixelSize: 10
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
            }

            Text {
                Layout.preferredWidth: 72
                text: "风险"
                color: Theme.textMuted
                font.pixelSize: 10
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                Layout.preferredWidth: 88
                visible: header.width > 650
                text: "最近修改"
                color: Theme.textMuted
                font.pixelSize: 10
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
            }

            Item { Layout.rightMargin: 12; Layout.preferredWidth: 18 }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }
    }
}
