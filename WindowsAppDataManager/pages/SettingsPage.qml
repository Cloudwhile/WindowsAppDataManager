pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

FocusAwareFlickable {
    id: page

    required property SettingsViewModel settingsController

    contentWidth: width
    contentHeight: contentColumn.implicitHeight + 48
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { }

    Column {
        id: contentColumn

        x: 24
        y: 22
        width: Math.min(parent.width - 48, 780)
        spacing: 16

        Column {
            width: parent.width
            spacing: 3

            Text {
                text: "设置"
                color: Theme.textPrimary
                font.pixelSize: 25
                font.weight: Font.DemiBold
            }

            Text {
                text: "调整应用外观与界面动效"
                color: Theme.textSecondary
                font.pixelSize: 12
            }
        }

        Text {
            text: "外观"
            color: Theme.textPrimary
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }

        Rectangle {
            width: parent.width
            height: appearanceOptions.implicitHeight
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            clip: true

            Column {
                id: appearanceOptions

                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: [
                        {
                            "value": 0,
                            "title": "跟随系统",
                            "description": "根据 Windows 当前配色自动选择浅色或深色",
                            "icon": Qt.resolvedUrl("../resources/Icons/TablerSettings.svg"),
                            "accent": Theme.accent
                        },
                        {
                            "value": 1,
                            "title": "浅色",
                            "description": "始终使用浅色界面",
                            "icon": Qt.resolvedUrl("../resources/Icons/TablerSunFilled.svg"),
                            "accent": Theme.amber
                        },
                        {
                            "value": 2,
                            "title": "深色",
                            "description": "始终使用深色界面",
                            "icon": Qt.resolvedUrl("../resources/Icons/TablerMoonFilled.svg"),
                            "accent": Theme.purple
                        }
                    ]

                    delegate: PreferenceOption {
                        id: appearanceOption

                        required property var modelData
                        width: appearanceOptions.width
                        title: modelData.title
                        description: modelData.description
                        iconSource: modelData.icon
                        accent: modelData.accent
                        selected: page.settingsController.themeMode === modelData.value
                        onFocusRequested: item => page.revealItem(item)
                        onClicked: page.settingsController.themeMode = modelData.value
                    }
                }
            }
        }

        Text {
            text: "动画"
            color: Theme.textPrimary
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }

        Rectangle {
            width: parent.width
            height: motionOptions.implicitHeight
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            clip: true

            Column {
                id: motionOptions

                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: [
                        {
                            "value": 0,
                            "title": "标准",
                            "description": "使用完整的页面、展开与状态过渡",
                            "icon": Qt.resolvedUrl("../resources/Icons/TablerActivityHeartbeat.svg"),
                            "accent": Theme.green
                        },
                        {
                            "value": 1,
                            "title": "减少",
                            "description": "缩短动效并移除缩放与大部分位移",
                            "icon": Qt.resolvedUrl("../resources/Icons/TablerArrowNarrowDown.svg"),
                            "accent": Theme.amber
                        },
                        {
                            "value": 2,
                            "title": "关闭",
                            "description": "关闭非必要界面动画",
                            "icon": Qt.resolvedUrl("../resources/Icons/TablerX.svg"),
                            "accent": Theme.red
                        }
                    ]

                    delegate: PreferenceOption {
                        id: motionOption

                        required property var modelData
                        width: motionOptions.width
                        title: modelData.title
                        description: modelData.description
                        iconSource: modelData.icon
                        accent: modelData.accent
                        selected: page.settingsController.motionPreference === modelData.value
                        onFocusRequested: item => page.revealItem(item)
                        onClicked: page.settingsController.motionPreference = modelData.value
                    }
                }
            }
        }

        Text {
            width: parent.width
            text: "更改会立即应用，并在下次启动时保留。"
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
