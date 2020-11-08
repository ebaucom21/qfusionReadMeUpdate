import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    ColumnLayout {
        spacing: 18
        width: parent.width - 32 - 128
        anchors.centerIn: parent

        InGameSelectorButton {
            text: "Main menu"
            onClicked: wsw.showMainMenu()
        }
        InGameSelectorButton {
            text: wsw.isSpectator ? "Join" : "Spectate"
        }
        InGameSelectorButton {
            text: "Gametype Options"
        }
        InGameSelectorButton {
            visible: wsw.isSpectator
            text: "Spectator options"
        }
        InGameSelectorButton {
            text: "Call a vote"
        }
        InGameSelectorButton {
            text: "Disconnect"
            onClicked: wsw.disconnect()
        }
    }
}