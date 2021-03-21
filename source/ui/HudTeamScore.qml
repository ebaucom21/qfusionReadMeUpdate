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
                target: colorBar
                anchors.left: undefined
                anchors.right: scoreLabel.left
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
                target: colorBar
                anchors.left: scoreLabel.right
                anchors.right: undefined
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
        width: parent.width - scoreLabel.width - 48
        anchors.top: parent.top
        anchors.margins: 8
        height: 16
        color: root.color
        radius: 2
        opacity: 0.5
    }

    Label {
        id: scoreLabel
        width: Math.min(implicitWidth, 72)
        anchors.top: parent.top
        anchors.topMargin: -8
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        font.weight: Font.ExtraBold
        font.letterSpacing: 3
        font.pointSize: 56
        style: Text.Raised
        text: score
    }

    Label {
        id: nameLabel
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        font.weight: Font.ExtraBold
        font.pointSize: 24
        font.letterSpacing: 4
        style: Text.Raised
        text: name
    }
}