import QtQuick

Rectangle {
    id: badge

    property int level: 0
    property string label: ""
    readonly property color accent: level === 0 ? Theme.greenText
                                    : level === 1 ? Theme.accentText
                                    : level === 2 ? Theme.amberText
                                    : level === 3 ? Theme.redText
                                    : level === 4 ? Theme.purpleText
                                                  : Theme.neutralText
    readonly property url iconSource: level === 0
                                          ? Qt.resolvedUrl("../resources/Icons/TablerCheck.svg")
                                          : level === 1
                                            ? Qt.resolvedUrl("../resources/Icons/TablerPointFilled.svg")
                                          : level === 2
                                            ? Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
                                          : level === 3
                                            ? Qt.resolvedUrl("../resources/Icons/TablerExclamationMark.svg")
                                          : level === 4
                                            ? Qt.resolvedUrl("../resources/Icons/TablerCancel.svg")
                                            : Qt.resolvedUrl("../resources/Icons/TablerQuestionMark.svg")

    implicitWidth: badgeContent.implicitWidth + 14
    implicitHeight: 22
    radius: Theme.radiusSmall
    color: level === 0 ? Theme.greenSoft
          : level === 1 ? Theme.accentSoft
          : level === 2 ? Theme.amberSoft
          : level === 3 ? Theme.redSoft
          : level === 4 ? Theme.purpleSoft
                        : Theme.neutralSoft

    Row {
        id: badgeContent

        anchors.centerIn: parent
        spacing: 4

        ThemedIcon {
            width: 11
            height: 11
            anchors.verticalCenter: parent.verticalCenter
            source: badge.iconSource
            color: badge.accent
        }

        Text {
            id: badgeText
            anchors.verticalCenter: parent.verticalCenter
            text: badge.label
            color: badge.accent
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }
}
