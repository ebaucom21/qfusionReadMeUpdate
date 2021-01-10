import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

MouseArea {
    id: root
    height: 36

    hoverEnabled: true

    property bool selected
    property bool detailed
    property string text

    Label {
        id: label
        text: root.text
        anchors.right: undefined
        anchors.horizontalCenter: parent.horizontalCenter

        color: (root.containsMouse || root.selected) ? Material.accent : Material.foreground
        font.weight: Font.Medium
        font.pointSize: 16
        font.letterSpacing: root.containsMouse ? 1.5 : 1.0
        font.capitalization: Font.AllUppercase

        transitions: Transition {
            AnchorAnimation { duration: 200 }
        }

        state: "initial"
        states: [
            State {
                name: "detailed"
                when: detailed
                AnchorChanges {
                    target: label
                    anchors.right: root.right
                    anchors.horizontalCenter: undefined
                }
            },
            State {
                name: "initial"
                when: !detailed
                AnchorChanges {
                    target: label
                    anchors.right: undefined
                    anchors.horizontalCenter: root.horizontalCenter
                }
            }
        ]
    }
}