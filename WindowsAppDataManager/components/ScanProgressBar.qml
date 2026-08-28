pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: bar

    required property real progress
    required property bool active
    property color accent: Theme.accent
    property color track: Theme.scanTrack
    property int segmentCount: 9

    readonly property real normalizedProgress: Math.max(0, Math.min(100, progress))
    readonly property int filledSegmentCount: normalizedProgress <= 0
                                              ? 0
                                              : Math.ceil(normalizedProgress
                                                          * segmentCount / 100)
    implicitWidth: 180
    implicitHeight: 7

    Accessible.role: Accessible.ProgressBar
    Accessible.name: active
                     ? "扫描进行中，总体进度 " + Math.round(normalizedProgress) + "%"
                     : "扫描进度 " + Math.round(normalizedProgress) + "%"

    Row {
        id: segments
        anchors.fill: parent
        spacing: 4

        Repeater {
            model: bar.segmentCount

            delegate: Rectangle {
                id: segment

                required property int index

                width: Math.max(0, (segments.width
                                    - segments.spacing * (bar.segmentCount - 1))
                                   / bar.segmentCount)
                height: segments.height
                radius: Math.min(2, height / 2)
                color: segment.index < bar.filledSegmentCount
                       ? bar.accent : bar.track

                Behavior on color {
                    ColorAnimation {
                        duration: Motion.fast
                        easing.type: Easing.Linear
                    }
                }
            }
        }
    }
}
