import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: quitPage
    property var backTrigger

    Popup {
        id: popup
        modal: true
        focus: true
        dim: false
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: UI.ui.desiredPopupWidth
        height: UI.ui.desiredPopupHeight

        function openSelf() {
            popup.parent = rootItem.windowContentItem
            rootItem.setOrUpdatePopupMode()
            popup.open()
        }

        function closeSelf() {
            if (opened) {
                popup.close()
                quitPage.backTrigger()
            }
        }

        onAboutToHide: rootItem.resetPopupMode()

        background: PopupBackground {}

        contentItem: PopupContentItem {
            title: "Quit the game?"
            active: popup.visible
            hasAcceptButton: true
            hasRejectButton: true
            acceptButtonText: "Go back"
            rejectButtonText: "Quit"
            onAccepted: popup.closeSelf()
            onRejected: UI.ui.quit()
            onDismissed: popup.closeSelf()
        }
    }

    Component.onCompleted: popup.openSelf()
    Component.onDestruction: popup.closeSelf()
}