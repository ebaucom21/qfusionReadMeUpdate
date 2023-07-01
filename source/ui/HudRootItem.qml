import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Window 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
    id: rootItem

    // These conditions try to prevent activating the loader until the status of models is well-defined.
    readonly property bool useDifferentHuds:
        hudDataModel.specLayoutModel.name.length > 0 && hudDataModel.clientLayoutModel.name.length > 0 &&
            hudDataModel.specLayoutModel.name.toUpperCase() != hudDataModel.clientLayoutModel.name.toUpperCase()

    Window.onWindowChanged: {
        if (Window.window) {
            Window.window.requestActivate()
            rootItem.forceActiveFocus()
        }
    }

    // Try reusing the same instance due to Qml GC quirks
    InGameHud {
        visible: wsw.isShowingHud && (hudDataModel.hasActivePov || !useDifferentHuds)
        anchors.fill: parent
        model: hudDataModel.clientLayoutModel
    }

    Loader {
        // Using separate HUD files for client and spec should be discouraged for now.
        active: useDifferentHuds
        anchors.fill: parent
        sourceComponent: InGameHud {
            // Toggle the visibility once it's loaded for the same GC-related reasons
            visible: wsw.isShowingHud && !hudDataModel.hasActivePov
            model: hudDataModel.specLayoutModel
        }
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