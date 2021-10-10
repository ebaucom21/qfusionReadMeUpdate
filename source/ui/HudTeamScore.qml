import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12

Item {
    id: root

    property int score
    property string name
    property string clan
    property color color
    property bool leftAligned
    property string playersStatus

    property real siblingNameWidth
    readonly property real nameWidth: nameLabel.implicitWidth
    readonly property real maxNameWidth: Math.max(nameWidth, siblingNameWidth)

    implicitWidth: scoreLabel.width + Math.max(270, maxNameWidth + 100)
    implicitHeight: 80
    width: implicitWidth
    height: implicitHeight

    state: "leftAligned"

    states: [
        State {
            name: "leftAligned"
            when: leftAligned
            AnchorChanges {
                target: scoreLabel
                anchors.left: undefined
                anchors.right: parent.right
            }
            AnchorChanges {
                target: nameLabel
                anchors.left: undefined
                anchors.right: scoreLabel.left
            }
            AnchorChanges {
                target: clanLabel
                anchors.left: undefined
                anchors.right: nameLabel.right
            }
            AnchorChanges {
                target: playersStatusIndicator
                anchors.left: colorBar.left
                anchors.right: undefined
            }
        },
        State {
            name: "rightAligned"
            when: !leftAligned
            AnchorChanges {
                target: scoreLabel
                anchors.left: parent.left
                anchors.right: undefined
            }
            AnchorChanges {
                target: nameLabel
                anchors.left: scoreLabel.right
                anchors.right: undefined
            }
            AnchorChanges {
                target: clanLabel
                anchors.left: nameLabel.left
                anchors.right: undefined
            }
            AnchorChanges {
                target: playersStatusIndicator
                anchors.left: undefined
                anchors.right: colorBar.right
            }
        }
    ]

    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: 0.7

        layer.enabled: true
        layer.effect: ElevationEffect { elevation: 16 }
    }

    Rectangle {
        id: colorBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 4
        radius: 1
        height: 28
        color: Qt.rgba(root.color.r, root.color.g, root.color.b, 1.0)
        opacity: wsw.isShowingScoreboard ? 0.5 : 0.6
    }

    Label {
        id: scoreLabel
        width: Math.max(96, implicitWidth)
        anchors.top: colorBar.top
        anchors.topMargin: -4
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        horizontalAlignment: Qt.AlignHCenter
        font.weight: Font.ExtraBold
        font.letterSpacing: 3
        font.pointSize: 56
        style: Text.Raised
        textFormat: Text.PlainText
        text: score
        transform: Scale {
            xScale: 1.00
            yScale: 0.85
        }
    }

    Label {
        id: clanLabel
        anchors.verticalCenter: colorBar.verticalCenter
        font.weight: Font.Black
        font.letterSpacing: 1.75
        font.pointSize: 16
        font.capitalization: Font.SmallCaps
        textFormat: Text.StyledText
        style: Text.Raised
        text: clan
    }

    Label {
        id: nameLabel
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: colorBar.height + 8 - 0.5 * nameLabel.height
        font.weight: Font.ExtraBold
        font.pointSize: 25
        font.letterSpacing: 1.25
        font.capitalization: Font.AllUppercase
        textFormat: Text.StyledText
        style: Text.Raised
        text: name
    }

    Label {
        id: playersStatusIndicator
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.verticalCenter: colorBar.verticalCenter
        anchors.verticalCenterOffset: +2
        font.family: wsw.symbolsFontFamily
        font.weight: Font.ExtraBold
        font.pointSize: 14
        style: Text.Raised
        textFormat: Text.StyledText
        text: playersStatus
    }
}