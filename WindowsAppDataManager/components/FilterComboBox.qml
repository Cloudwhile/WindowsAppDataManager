import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    implicitHeight: 36
    leftPadding: 10
    rightPadding: 34
    font.pixelSize: 11

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
