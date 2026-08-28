pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    implicitHeight: 36
    leftPadding: 10
    rightPadding: 34
    font.pixelSize: 11

    delegate: ItemDelegate {
        id: option

        required property int index
        required property var modelData

        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: 34
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        highlighted: control.highlightedIndex === index
        text: control.textAt(index)

        background: InsetStateLayer {
            selected: option.index === control.currentIndex
            hovered: option.highlighted || option.hovered
            pressed: option.down
            focused: option.activeFocus
        }

        contentItem: Text {
            text: option.text
            color: option.index === control.currentIndex
                   ? Theme.accentText : Theme.textSecondary
            font.family: control.font.family
            font.pixelSize: control.font.pixelSize
            font.weight: option.index === control.currentIndex ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    contentItem: Text {
        text: control.displayText
        color: control.enabled ? Theme.textSecondary : Theme.textMuted
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: ThemedIcon {
        x: control.width - width - 10
        y: Math.round((control.height - height) / 2)
        width: 14
        height: 14
        source: Qt.resolvedUrl("../resources/Icons/TablerChevronDownFilled.svg")
        color: control.activeFocus || control.popup.visible ? Theme.accent : Theme.textMuted
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceRaised
        border.width: 1
        border.color: control.activeFocus || control.popup.visible ? Theme.accent : Theme.border
    }
}
