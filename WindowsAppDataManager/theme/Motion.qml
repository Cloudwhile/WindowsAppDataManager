pragma Singleton

import QtQuick

QtObject {
    // 0: 标准，1: 减少，2: 关闭
    property int preference: 0

    readonly property int instant: preference === 2 ? 0 : 80
    readonly property int fast: preference === 2 ? 0 : preference === 1 ? 90 : 140
    readonly property int normal: preference === 2 ? 0 : preference === 1 ? 140 : 220
    readonly property int slow: preference === 2 ? 0 : preference === 1 ? 190 : 320
    readonly property int emphasized: preference === 2 ? 0 : preference === 1 ? 230 : 420
    readonly property bool allowPosition: preference === 0
    readonly property bool allowScale: preference === 0
}
