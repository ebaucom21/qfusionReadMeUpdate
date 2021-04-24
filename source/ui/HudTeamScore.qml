import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12

Item {
    id: root

    property int score
    property string name
    property color color
    property bool leftAligned
    property string teamStatus
    property string playersStatus

    implicitWidth: 360
    implicitHeight: 72
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
                target: teamStatusIndicator
                anchors.left: colorBar.left
                anchors.right: undefined
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
                target: teamStatusIndicator
                anchors.left: undefined
                anchors.right: colorBar.right
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
        color: Qt.rgba(root.color.r, root.color.g, root.color.b, wsw.isShowingScoreboard ? 0.5 : 0.6)
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
        text: score
        transform: Scale {
            xScale: 1.00
            yScale: 0.85
        }
    }

    Label {
        id: nameLabel
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        font.weight: Font.ExtraBold
        font.pointSize: 24
        font.letterSpacing: 3
        style: Text.Raised
        text: name
    }

    Label {
        id: teamStatusIndicator
        text: teamStatus
        anchors.margins: 4
        anchors.bottom: parent.bottom
        font.weight: Font.ExtraBold
        font.pointSize: 24
        style: Text.Raised
    }

    Label {
        id: playersStatusIndicator
        text: playersStatus
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.verticalCenter: colorBar.verticalCenter
        font.weight: Font.ExtraBold
        font.pointSize: 20
        style: Text.Raised
    }
}