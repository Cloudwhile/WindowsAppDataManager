import QtQuick
import QtQuick.Layouts

Item {
    id: row

    required property var rowData
    property bool selected: false

    signal activated(string applicationId)

    height: 46
    activeFocusOnTab: true

    InsetStateLayer {
        anchors.fill: parent
        selected: row.selected
        hovered: mouseArea.containsMouse
        focused: row.activeFocus
        focusWidth: 2
    }

    Accessible.role: Accessible.ListItem
    Accessible.name: rowData.appName + "，" + rowData.sizeText + "，" + rowData.riskText
    Accessible.description: "路径 " + rowData.location + "，分类 " + rowData.category
                            + "，文件数 " + rowData.fileCount
    Accessible.onPressAction: row.activated(row.rowData.appId)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        ApplicationIcon {
            Layout.preferredWidth: 26
            Layout.preferredHeight: 26
            iconSource: row.rowData.iconSource || ""
            shortName: row.rowData.shortName
            accentIndex: row.rowData.accentIndex
        }

        Text {
            Layout.preferredWidth: 150
            Layout.minimumWidth: 100
            Layout.fillWidth: true
            text: row.rowData.appName
            color: Theme.textPrimary
            font.pixelSize: 13
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Text {
            Layout.preferredWidth: 174
            Layout.minimumWidth: 116
            visible: row.width > 740
            text: row.rowData.location
            color: Theme.textSecondary
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }

        Text {
            Layout.preferredWidth: 82
            Layout.maximumWidth: 96
            visible: row.width > 610
            text: row.rowData.category
            color: Theme.textSecondary
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        Text {
            Layout.preferredWidth: 70
            text: row.rowData.sizeText
            color: Theme.textPrimary
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
        }

        Text {
            Layout.preferredWidth: 64
            visible: row.width > 720
            text: row.rowData.fileCount
            color: Theme.textSecondary
            font.pixelSize: 11
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
        }

        RiskBadge {
            Layout.preferredWidth: 72
            level: row.rowData.riskLevel
            label: row.rowData.riskText
        }

        Text {
            Layout.preferredWidth: 88
            visible: row.width > 1000
            text: row.rowData.modified
            color: Theme.textMuted
            font.pixelSize: 11
            horizontalAlignment: Text.AlignRight
        }

        ThemedIcon {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            source: Qt.resolvedUrl("../resources/Icons/TablerChevronRight.svg")
            color: mouseArea.containsMouse || row.activeFocus ? Theme.accent : Theme.textMuted
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        visible: !row.activeFocus
        color: Theme.divider
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            row.forceActiveFocus()
            row.activated(row.rowData.appId)
        }
    }

    Keys.onReturnPressed: row.activated(row.rowData.appId)
    Keys.onEnterPressed: row.activated(row.rowData.appId)
    Keys.onSpacePressed: row.activated(row.rowData.appId)
}
