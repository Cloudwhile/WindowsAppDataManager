import QtQuick
import QtQuick.Layouts

Rectangle {
    id: row

    required property var rowData
    required property int rowIndex
    property bool selected: false

    signal activated(int index)

    height: 50
    activeFocusOnTab: true
    color: selected ? Theme.surfaceSelected
                    : activeFocus || mouseArea.containsMouse ? Theme.surfaceHover : "transparent"

    Accessible.role: Accessible.ListItem
    Accessible.name: rowData.appName + "，" + rowData.sizeText + "，" + rowData.riskText

    Behavior on color {
        ColorAnimation { duration: Motion.fast }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            radius: 7
            color: row.rowData.accent

            Text {
                anchors.centerIn: parent
                text: row.rowData.shortName
                color: "#ffffff"
                font.pixelSize: row.rowData.shortName.length > 2 ? 8 : 11
                font.weight: Font.Bold
            }
        }

        Text {
            Layout.preferredWidth: 150
            Layout.minimumWidth: 105
            Layout.fillWidth: true
            text: row.rowData.appName
            color: Theme.textPrimary
            font.pixelSize: 13
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Text {
            Layout.preferredWidth: 92
            Layout.maximumWidth: 110
            visible: row.width > 560
            text: row.rowData.category
            color: Theme.textSecondary
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Text {
            Layout.preferredWidth: 70
            text: row.rowData.sizeText
            color: Theme.textPrimary
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
        }

        RiskBadge {
            Layout.preferredWidth: implicitWidth
            level: row.rowData.riskLevel
            label: row.rowData.riskText
        }

        Text {
            Layout.preferredWidth: 88
            visible: row.width > 650
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
        color: Theme.divider
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            row.forceActiveFocus()
            row.activated(row.rowIndex)
        }
    }

    Keys.onReturnPressed: row.activated(row.rowIndex)
    Keys.onEnterPressed: row.activated(row.rowIndex)
    Keys.onSpacePressed: row.activated(row.rowIndex)
}
