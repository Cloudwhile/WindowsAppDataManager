pragma Singleton

import QtQuick

Item {
    id: theme

    visible: false
    width: 0
    height: 0

    // 0: 跟随系统，1: 浅色，2: 深色
    property int mode: 0

    SystemPalette {
        id: systemPalette
        colorGroup: SystemPalette.Active
    }

    readonly property bool systemDark: systemPalette.window.hslLightness < 0.5
    readonly property bool dark: mode === 2 || (mode === 0 && systemDark)
    readonly property string modeName: mode === 0 ? "跟随系统" : mode === 1 ? "浅色" : "深色"

    readonly property color canvas: dark ? "#11151a" : "#f3f5f7"
    readonly property color surface: dark ? "#191e25" : "#ffffff"
    readonly property color surfaceRaised: dark ? "#20262e" : "#f9fafb"
    readonly property color surfaceHover: dark ? "#252d36" : "#eef4fb"
    readonly property color surfaceSelected: dark ? "#173351" : "#e5f1ff"
    readonly property color border: dark ? "#303944" : "#dce2e8"
    readonly property color divider: dark ? "#29313a" : "#e7ebef"

    readonly property color textPrimary: dark ? "#f3f6f9" : "#1d2733"
    readonly property color textSecondary: dark ? "#aeb9c5" : "#5f6d7a"
    readonly property color textMuted: dark ? "#8e9aa6" : "#697582"

    readonly property color accent: dark ? "#58a8ff" : "#1473e6"
    readonly property color accentStrong: dark ? "#75b8ff" : "#075fc2"
    readonly property color accentSoft: dark ? "#163d64" : "#dcecff"
    readonly property color green: dark ? "#56cf88" : "#1f9d55"
    readonly property color greenSoft: dark ? "#153c2a" : "#ddf5e6"
    readonly property color amber: dark ? "#f6b94d" : "#d97a08"
    readonly property color amberSoft: dark ? "#493417" : "#fff0d5"
    readonly property color red: dark ? "#ff7d7d" : "#d9363e"
    readonly property color redSoft: dark ? "#4a2227" : "#fde6e7"
    readonly property color purple: dark ? "#b69cff" : "#7657c8"
    readonly property color purpleSoft: dark ? "#352d4d" : "#eee8ff"
    readonly property color cyan: dark ? "#52c7d8" : "#15879a"
    readonly property color neutral: dark ? "#a0aab5" : "#687582"
    readonly property color neutralSoft: dark ? "#2b323a" : "#e9edf1"

    readonly property color accentText: dark ? "#8fc8ff" : "#075fc2"
    readonly property color greenText: dark ? "#79dfa4" : "#167a41"
    readonly property color amberText: dark ? "#ffd17a" : "#9a5200"
    readonly property color redText: dark ? "#ffaaaa" : "#b4232b"
    readonly property color purpleText: dark ? "#cbbcff" : "#6041a8"
    readonly property color neutralText: dark ? "#c0c8d1" : "#56616d"
    readonly property color onAccent: dark ? "#071624" : "#ffffff"
    readonly property color scanTrack: dark ? "#294d72" : "#bed8f7"

    readonly property int radiusSmall: 5
    readonly property int radiusMedium: 9
    readonly property int radiusLarge: 13

    function applicationAccent(index) {
        const lightPalette = ["#1769aa", "#6741b6", "#0b6f87",
                              "#08786f", "#7c3fa0", "#954b16"]
        const darkPalette = ["#58a8ff", "#b69cff", "#52c7d8",
                             "#56cf88", "#f6b94d", "#ff9b78"]
        const palette = dark ? darkPalette : lightPalette
        return palette[Math.max(0, index) % palette.length]
    }

    function applicationAccentText(index) {
        return dark ? "#071624" : "#ffffff"
    }

    function cycleMode() {
        mode = (mode + 1) % 3
    }
}
