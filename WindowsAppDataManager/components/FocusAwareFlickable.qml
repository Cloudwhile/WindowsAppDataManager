import QtQuick

Flickable {
    id: control

    property real focusRevealMargin: 12

    function revealItem(item) {
        if (!item)
            return

        const position = item.mapToItem(control.contentItem, 0, 0)
        const itemTop = position.y - control.focusRevealMargin
        const itemBottom = position.y + item.height + control.focusRevealMargin
        const maximumContentY = Math.max(0, control.contentHeight - control.height)

        if (itemTop < control.contentY)
            control.contentY = Math.max(0, itemTop)
        else if (itemBottom > control.contentY + control.height)
            control.contentY = Math.min(maximumContentY, itemBottom - control.height)
    }
}
