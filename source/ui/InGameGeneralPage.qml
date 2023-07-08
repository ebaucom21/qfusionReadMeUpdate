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
            onClicked: UI.ui.showMainMenu()
        }
        InGameButton {
            visible: UI.ui.canBeReady
            text: UI.ui.isReady ? "Not ready" : "Ready"
            onClicked: {
                UI.ui.toggleReady()
                UI.ui.returnFromInGameMenu()
            }
        }
        InGameButton {
            visible: (!UI.hudDataModel.hasTwoTeams && UI.ui.canJoin) ||
                     (UI.hudDataModel.hasTwoTeams && UI.ui.canJoinAlpha && UI.ui.canJoinBeta)
            text: "Join"
            onClicked: {
                UI.ui.join()
                UI.ui.returnFromInGameMenu()
            }
        }
        InGameButton {
            visible: UI.ui.canToggleChallengerStatus
            text: UI.ui.isInChallengersQueue ? "Leave the queue" : "Enter the queue"
            onClicked: {
                UI.ui.toggleChallengerStatus()
                UI.ui.returnFromInGameMenu()
            }
        }
        InGameButton {
            visible: UI.ui.canJoinAlpha
            text: "Join '" + UI.hudDataModel.alphaName + "'"
            onClicked: {
                UI.ui.joinAlpha()
                UI.ui.returnFromInGameMenu()
            }
        }
        InGameButton {
            visible: UI.ui.canJoinBeta
            text: "Join '" + UI.hudDataModel.betaName + "'"
            onClicked: {
                UI.ui.joinBeta()
                UI.ui.returnFromInGameMenu()
            }
        }
        InGameButton {
            visible: UI.ui.canSpectate
            text: "Spectate"
            onClicked: {
                UI.ui.spectate()
                UI.ui.returnFromInGameMenu()
            }
        }
        InGameButton {
            text: "Disconnect"
            onClicked: UI.ui.disconnect()
        }
    }
}