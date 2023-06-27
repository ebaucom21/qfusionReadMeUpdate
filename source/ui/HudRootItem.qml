import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Window 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
    id: rootItem

    property bool isBlurringBackground
    property bool isBlurringLimitedToRect
    property rect blurRect

    Window.onWindowChanged: {
        if (Window.window) {
            Window.window.requestActivate()
            rootItem.forceActiveFocus()
        }
    }

    InGameHud {
        id: inGameHud
        visible: wsw.isShowingHud
        anchors.fill: parent
        focus: true
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