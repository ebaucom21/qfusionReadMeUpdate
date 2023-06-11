import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Window 2.12
import QtGraphicalEffects 1.12
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

    Keys.forwardTo: [
        mainMenuLoader.item, connectionScreenLoader.item,
        demoPlaybackMenuLoader.item, inGameMenuLoader.item, hudLoader.item
    ]

    Item {
        id: shaderSourceItem
        anchors.fill: parent

        Loader {
            id: mainMenuLoader
            active: wsw.isShowingMainMenu
            anchors.fill: parent
            sourceComponent: MainMenu {}
        }

        Loader {
            id: connectionScreenLoader
            active: wsw.isShowingConnectionScreen
            anchors.fill: parent
            sourceComponent: ConnectionScreen {}
        }

        Loader {
            id: demoPlaybackMenuLoader
            active: wsw.isShowingDemoPlaybackMenu
            anchors.fill: parent
            sourceComponent: DemoPlaybackMenu {}
        }

        Loader {
            id: inGameMenuLoader
            active: wsw.isShowingInGameMenu
            anchors.fill: parent
            sourceComponent: InGameMenu {}
        }

        Loader {
            active: wsw.isShowingScoreboard
            anchors.fill: parent
            sourceComponent: ScoreboardScreen {}
        }

        Loader {
            id: hudLoader
            active: wsw.isShowingHud || wsw.isShowingChatPopup || wsw.isShowingTeamChatPopup
            anchors.fill: parent
            sourceComponent: InGameHud {}
        }

        Loader {
            active: wsw.isShowingActionRequests
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.margins: 16
            width: 480
            sourceComponent: ActionRequestArea {}
        }
    }

    ShaderEffectSource {
        visible: popupOverlay.visible
        id: effectSource
        hideSource: popupOverlay.visible
        anchors.centerIn: shaderSourceItem
        width: popupOverlay.visible ? sourceItem.width : 0
        height: popupOverlay.visible ? sourceItem.height : 0
        sourceItem: shaderSourceItem
    }

    FastBlur {
        visible: popupOverlay.visible
        anchors.fill: effectSource
        source: effectSource
        radius: 64
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