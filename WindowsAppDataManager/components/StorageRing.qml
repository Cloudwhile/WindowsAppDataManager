import QtQuick

Item {
    id: ring

    property real localValue: 54.6
    property real roamingValue: 31.7
    property real lowValue: 13.7
    property string totalText: "24.6"
    property string totalLabel: "GB"

    implicitWidth: 154
    implicitHeight: 154

    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            const context = getContext("2d")
            context.reset()

            const centerX = width / 2
            const centerY = height / 2
            const radius = Math.min(width, height) / 2 - 15
            const values = [ring.localValue, ring.roamingValue, ring.lowValue]
            const colors = [Theme.accent, Theme.green, Theme.amber]
            let start = -Math.PI / 2

            context.lineWidth = 18
            context.lineCap = "butt"
            context.beginPath()
            context.strokeStyle = Theme.divider
            context.arc(centerX, centerY, radius, 0, Math.PI * 2)
            context.stroke()

            for (let index = 0; index < values.length; ++index) {
                const sweep = Math.PI * 2 * values[index] / 100
                if (sweep <= 0.05) {
                    start += sweep
                    continue
                }
                context.beginPath()
                context.strokeStyle = colors[index]
                context.arc(centerX, centerY, radius, start + 0.025, start + sweep - 0.025)
                context.stroke()
                start += sweep
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: -1

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: ring.totalText
            color: Theme.textPrimary
            font.pixelSize: ring.totalText.length > 8 ? 16 : 20
            font.weight: Font.DemiBold
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: ring.totalLabel
            color: Theme.textMuted
            font.pixelSize: 11
        }
    }

    Connections {
        target: Theme
        function onDarkChanged() { canvas.requestPaint() }
    }

    onLocalValueChanged: canvas.requestPaint()
    onRoamingValueChanged: canvas.requestPaint()
    onLowValueChanged: canvas.requestPaint()
}
