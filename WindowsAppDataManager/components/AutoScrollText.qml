import QtQuick
import QtQuick.Window

Item {
    id: root

    property alias text: label.text
    property alias color: label.color
    property alias font: label.font
    property bool running: false
    property bool playOnce: false
    property bool preservePositionOnUpdate: false
    property bool paused: false
    property real pixelsPerSecond: 28
    property real leadingPause: 0.9
    property real trailingPause: 0.8

    property real offset: 0
    property int direction: 1
    property real holdRemaining: leadingPause
    property bool cycleCompleted: false

    readonly property real maximumOffset: Math.max(0, label.implicitWidth - width)
    readonly property bool overflow: maximumOffset > 0.5
    readonly property bool windowPaused: root.Window.window === null
                                         || !root.Window.window.visible
                                         || root.Window.window.visibility === Window.Hidden
                                         || root.Window.window.visibility === Window.Minimized
    readonly property bool animationAllowed: overflow
                                             && !paused
                                             && !windowPaused
                                             && (running
                                                 || (playOnce && !cycleCompleted))

    clip: true

    function resetCycle() {
        offset = 0
        direction = 1
        holdRemaining = leadingPause
        cycleCompleted = false
    }

    function advance(frameSeconds) {
        const elapsed = Math.min(frameSeconds, 0.05)
        if (holdRemaining > 0) {
            holdRemaining = Math.max(0, holdRemaining - elapsed)
            return
        }

        const targetOffset = direction > 0 ? maximumOffset : 0
        const distance = pixelsPerSecond * elapsed
        if (direction > 0) {
            offset = Math.min(targetOffset, offset + distance)
            if (offset >= targetOffset - 0.5) {
                offset = targetOffset
                direction = -1
                holdRemaining = trailingPause
            }
            return
        }

        offset = Math.max(0, offset - distance)
        if (offset <= 0.5) {
            offset = 0
            direction = 1
            cycleCompleted = !running
            holdRemaining = leadingPause
        }
    }

    onRunningChanged: resetCycle()
    onPlayOnceChanged: resetCycle()
    onTextChanged: {
        if (!running || !preservePositionOnUpdate)
            resetCycle()
    }
    onMaximumOffsetChanged: {
        offset = Math.max(0, Math.min(offset, maximumOffset))
        if (!overflow)
            resetCycle()
    }
    Component.onCompleted: resetCycle()
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Text {
        id: label

        x: -root.offset
        anchors.verticalCenter: parent.verticalCenter
        wrapMode: Text.NoWrap
        elide: Text.ElideNone
    }

    FrameAnimation {
        running: root.animationAllowed
        onTriggered: root.advance(frameTime)
    }
}
