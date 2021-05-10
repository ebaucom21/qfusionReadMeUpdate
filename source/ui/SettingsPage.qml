import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    readonly property var handleKeyEvent: swipeView.currentItem["handleKeyEvent"]

    WswTabBar {
        id: tabBar
        enabled: !wsw.hasPendingCVarChanges
        background: null
        currentIndex: swipeView.currentIndex

        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }

        WswTabButton { text: "General" }
        WswTabButton { text: "Teams" }
        WswTabButton { text: "Graphics" }
        WswTabButton { text: "Sound" }
        WswTabButton { text: "Mouse" }
        WswTabButton { text: "Keyboard" }
        WswTabButton { text: "HUD" }
    }

    SwipeView {
        id: swipeView
        interactive: !wsw.hasPendingCVarChanges
        clip: true

        anchors {
            top: tabBar.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }

        Connections {
            target: tabBar
            onCurrentIndexChanged: swipeView.currentIndex = tabBar.currentIndex
        }

        GeneralSettings {}
        TeamsSettings {}
        GraphicsSettings {}
        SoundSettings {}
        MouseSettings {}
        KeyboardSettings {}
        HudSettings {}
    }

    Loader {
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
        }

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