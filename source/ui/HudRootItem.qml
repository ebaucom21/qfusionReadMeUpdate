import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Window 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
    id: rootItem

    property var instantiatedMiniviewHuds: ({})

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
        layoutModel: Hud.commonDataModel.regularLayoutModel
        commonDataModel: Hud.commonDataModel
        povDataModel: Hud.povDataModel
        miniviewAllocator: rootItem
    }

    Component.onCompleted: {
        applyMiniviewLayoutPass1()
        applyMiniviewLayoutPass2()
    }

    Connections {
        target: Hud.commonDataModel
        onMiniviewLayoutChangedPass1: applyMiniviewLayoutPass1()
        onMiniviewLayoutChangedPass2: applyMiniviewLayoutPass2()
    }

    Connections {
        target: Hud.ui
        onShuttingDown: {
            for (const view of Object.values(rootItem.instantiatedMiniviewHuds)) {
                view.parent = null
                view.destroy()
            }
            rootItem.instantiatedMiniviewHuds = null
        }
    }

    function applyMiniviewLayoutPass1() {
    }
    function applyMiniviewLayoutPass2() {
    }

    function recycle(view, miniviewIndex) {
        view.parent = null
        instantiatedMiniviewHuds[miniviewIndex] = view
    }

    function take(miniviewIndex) {
        const maybeExistingView = instantiatedMiniviewHuds[miniviewIndex]
        if (maybeExistingView) {
            return maybeExistingView
        }
        const model = Hud.commonDataModel.getMiniviewModelForIndex(miniviewIndex)
        const view  = miniviewComponent.createObject(rootItem, {"povDataModel" : model})
        instantiatedMiniviewHuds[miniviewIndex] = view
        return view
    }

    Component {
        id: miniviewComponent
        InGameHud {
            layoutModel: Hud.commonDataModel.miniviewLayoutModel
            commonDataModel: Hud.commonDataModel
            // pov data model specified via args

            Rectangle {
                anchors.centerIn: parent
                width: parent.width - 4
                height: parent.height - 4
                color: "transparent"
                border.color: "white"
                border.width: 1
            }
        }
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