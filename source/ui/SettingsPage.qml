import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    readonly property var handleKeyEvent: stackView.currentItem["handleKeyEvent"]

    Component {
        id: generalSettingsComponent
        GeneralSettings {}
    }

    WswTabBar {
        id: tabBar
        enabled: !wsw.hasPendingCVarChanges
        background: null
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right

        onCurrentItemChanged: stackView.replace(currentItem.component)

        WswTabButton {
            readonly property var component: generalSettingsComponent
            text: "General"
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
        initialItem: generalSettingsComponent
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
            readonly property color defaultForegroundColor: Material.foreground
            readonly property color defaultBackgroundColor: Material.background

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                radius: 2
                width: parent.width - 20
                height: parent.height - 20
                color: Material.accent
            }

            Button {
                anchors {
                    verticalCenter: parent.verticalCenter
                    right: parent.horizontalCenter
                }
                text: "Revert"
                flat: true
                Material.foreground: applyChangesPane.defaultBackgroundColor
                onClicked: wsw.rollbackPendingCVarChanges()
            }

            Button {
                anchors {
                    verticalCenter: parent.verticalCenter
                    left: parent.horizontalCenter
                }
                text: "Accept"
                flat: true
                Material.foreground: applyChangesPane.defaultForegroundColor
                onClicked: wsw.commitPendingCVarChanges()
            }
        }
    }
}