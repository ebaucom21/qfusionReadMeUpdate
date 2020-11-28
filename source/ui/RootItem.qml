import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Window 2.12
import net.warsow 2.6

Item {
    id: rootItem

    property var windowContentItem

    Window.onWindowChanged: {
        if (Window.window) {
            Window.window.requestActivate()
            rootItem.forceActiveFocus()
            rootItem.windowContentItem = Window.window.contentItem
        }
    }

    Keys.forwardTo: [mainMenu, connectionScreen, demoPlaybackMenu, inGameMenu]

    // TODO: These items should be wrapped in loaders
    MainMenu {
        id: mainMenu
    }
    ConnectionScreen {
        id: connectionScreen
    }
    DemoPlaybackMenu {
        id: demoPlaybackMenu
    }
    InGameMenu {
        id: inGameMenu
    }
    Loader {
        active: wsw.isShowingScoreboard
        anchors.fill: parent
        sourceComponent: Component { ScoreboardScreen {} }
    }

    MouseArea {
        id: popupOverlay
        visible: false
        hoverEnabled: true
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            color: "#3A885500"
        }
    }

    function enablePopupOverlay() {
        popupOverlay.visible = true
    }

    function disablePopupOverlay() {
        popupOverlay.visible = false
    }
}