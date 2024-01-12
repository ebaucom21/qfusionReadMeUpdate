import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Window 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
    id: rootItem

    Window.onWindowChanged: {
        if (Window.window) {
            Window.window.requestActivate()
            rootItem.forceActiveFocus()
        }
    }

    // Try reusing the same instance due to Qml GC quirks
    InGameHud {
        // TODO: Is visibility switching it really needed (we don't draw it anyway, but property updates handling may vary)?
        visible: Hud.ui.isShowingHud
        anchors.fill: parent
        layoutModel: Hud.commonDataModel.layoutModel
        commonDataModel: Hud.commonDataModel
        povDataModel: Hud.povDataModel
    }

    Loader {
        active: Hud.ui.isShowingActionRequests
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 16
        width: 480
        sourceComponent: ActionRequestArea {}
    }
}