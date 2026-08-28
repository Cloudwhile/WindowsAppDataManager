import QtQuick

Item {
    id: layer

    property bool selected: false
    property bool hovered: false
    property bool pressed: false
    property bool focused: false
    property int inset: 4
    property real cornerRadius: Theme.radiusSmall
    property color idleColor: "transparent"
    property color selectedColor: Theme.surfaceSelected
    property color hoverColor: Theme.surfaceHover
    property color pressedColor: Theme.surfaceSelected
    property color focusColor: Theme.accent
    property int focusWidth: 1
    property int idleBorderWidth: 0
    property color idleBorderColor: "transparent"

    Rectangle {
        anchors.fill: parent
        anchors.margins: Math.max(0, layer.inset)
        radius: layer.cornerRadius
        color: layer.pressed ? layer.pressedColor
                             : layer.selected ? layer.selectedColor
                                              : layer.hovered ? layer.hoverColor
                                                              : layer.idleColor
        border.width: layer.focused ? layer.focusWidth : layer.idleBorderWidth
        border.color: layer.focused ? layer.focusColor : layer.idleBorderColor

        Behavior on color {
            ColorAnimation {
                duration: layer.selected || layer.hovered || layer.pressed
                          ? Motion.hoverEnter : Motion.hoverExit
                easing.type: Easing.OutCubic
            }
        }
    }
}
