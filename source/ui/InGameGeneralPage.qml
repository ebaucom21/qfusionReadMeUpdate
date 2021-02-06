import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    ColumnLayout {
        spacing: 20
        width: parent.width - 32 - 192
        anchors.centerIn: parent

        InGameButton {
            text: "Main menu"
            onClicked: wsw.showMainMenu()
        }
        InGameButton {
            text: wsw.isSpectator ? "Join" : "Spectate"
        }
        InGameButton {
            text: "Disconnect"
            onClicked: wsw.disconnect()
        }
    }
}