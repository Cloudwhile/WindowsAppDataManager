import QtQuick
import QtQuick.Controls.impl

Item {
    id: icon

    required property url source
    property color color: Theme.textSecondary
    property int sourceSize: Math.ceil(Math.max(width, height) * Math.max(1, Screen.devicePixelRatio))

    IconImage {
        anchors.fill: parent
        source: icon.source
        color: icon.color
        sourceSize.width: icon.sourceSize
        sourceSize.height: icon.sourceSize
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        cache: true
    }
}
