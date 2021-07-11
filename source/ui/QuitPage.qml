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
        width: wsw.desiredPopupWidth
        height: wsw.desiredPopupHeight

        function openSelf() {
            popup.parent = rootItem.windowContentItem
            rootItem.enablePopupOverlay()
            popup.open()
        }

        function closeSelf() {
            if (opened) {
                popup.close()
                rootItem.disablePopupOverlay()
                quitPage.backTrigger()
            }
        }

        background: PopupBackground {}

        contentItem: PopupContentItem {
            title: "Quit the game?"
            hasAcceptButton: true
            hasRejectButton: true
            acceptButtonText: "Go back"
            rejectButtonText: "Quit"
            onAccepted: popup.closeSelf()
            onRejected: wsw.quit()
        }
    }

    Component.onCompleted: popup.openSelf()
    Component.onDestruction: popup.closeSelf()
}