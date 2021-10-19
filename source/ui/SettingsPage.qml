import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    readonly property var handleKeyEvent: stackView.currentItem["handleKeyEvent"]

    Component {
        id: playerSettingsComponent
        PlayerSettings {}
    }

    // A safety guard
    Component.onDestruction: wsw.rollbackPendingCVarChanges()

    WswTabBar {
        id: tabBar
        enabled: !wsw.hasPendingCVarChanges
        background: null
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right

        onCurrentItemChanged: stackView.replace(currentItem.component)

        WswTabButton {
            readonly property var component: playerSettingsComponent
            text: "Player"
        }
        WswTabButton {
            readonly property var component: Component { TeamsSettings {} }
            text: "Teams"
        }
        WswTabButton {
            readonly property var component: Component { GraphicsSettings {} }
            text: "Graphics"
        }
        WswTabButton {
            readonly property var component: Component { SoundSettings {} }
            text: "Sound"
        }
        WswTabButton {
            readonly property var component: Component { MouseSettings {} }
            text: "Mouse"
        }
        WswTabButton {
            readonly property var component: Component { KeyboardSettings {} }
            text: "Keyboard"
        }
        WswTabButton {
            readonly property var component: Component { HudSettings {} }
            text: "HUD"
        }
    }

    StackView {
        id: stackView
        anchors.top: tabBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        initialItem: playerSettingsComponent
        clip: false
    }

    Loader {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: 64
        active: wsw.hasPendingCVarChanges
        sourceComponent: applyChangesComponent
    }

    Component {
        id: applyChangesComponent

        Item {
            id: applyChangesPane

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                radius: 2
                width: parent.width - 20
                height: parent.height - 20
                color: Qt.lighter(Material.background, 1.5)
            }

            Button {
                anchors {
                    verticalCenter: parent.verticalCenter
                    right: parent.horizontalCenter
                    rightMargin: 8
                }
                text: "Revert"
                flat: true
                onClicked: wsw.rollbackPendingCVarChanges()
            }

            Button {
                anchors {
                    verticalCenter: parent.verticalCenter
                    left: parent.horizontalCenter
                    leftMargin: 8
                }
                text: "Accept"
                flat: false
                highlighted: true
                onClicked: wsw.commitPendingCVarChanges()
            }
        }
    }
}