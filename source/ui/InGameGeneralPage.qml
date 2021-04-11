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
            visible: (!hudDataModel.hasTwoTeams && wsw.canJoin) ||
                     (hudDataModel.hasTwoTeams && wsw.canJoinAlpha && wsw.canJoinBeta)
            text: "Join"
            onClicked: {
                wsw.join()
                wsw.returnFromInGameMenu()
            }
        }
        InGameButton {
            visible: wsw.canJoinAlpha
            text: "Join '" + hudDataModel.alphaName + "'"
            onClicked: {
                wsw.joinAlpha()
                wsw.returnFromInGameMenu()
            }
        }
        InGameButton {
            visible: wsw.canJoinBeta
            text: "Join '" + hudDataModel.betaName + "'"
            onClicked: {
                wsw.joinBeta()
                wsw.returnFromInGameMenu()
            }
        }
        InGameButton {
            visible: wsw.canSpectate
            text: "Spectate"
            onClicked: {
                wsw.spectate()
                wsw.returnFromInGameMenu()
            }
        }
        InGameButton {
            text: "Disconnect"
            onClicked: wsw.disconnect()
        }
    }
}