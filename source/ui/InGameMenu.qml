import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Rectangle {
    id: root
    color: UI.ui.colorWithAlpha(Material.background, UI.ui.fullscreenOverlayOpacity)

    readonly property bool canShowLoadouts: UI.gametypeOptionsModel.available && !UI.hudDataModel.isSpectator

    // Force redrawing stuff every frame
    ProgressBar {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        indeterminate: true
        Material.accent: parent.Material.background
    }

    CarouselTabBar {
        id: tabBar
        visible: stackView.depth < 2
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: canShowLoadouts ? Math.min(1.25 * mainPane.width, parent.width) : mainPane.width
        height: implicitHeight
        model: [
            {"text" : "General", "component": generalComponent},
            {"text" : "Chat", "component" : chatComponent},
            {"text" : "Callvotes", "component" : callvotesComponent},
        ]
        onCurrentIndexChanged: {
            // replace the entire stack
            stackView.replace(null, model[currentIndex]["component"])
        }
        
        Component.onCompleted: UI.ui.registerHudOccluder(tabBar)
        Component.onDestruction: UI.ui.unregisterHudOccluder(tabBar)
        onWidthChanged: UI.ui.updateHudOccluder(tabBar)
        onHeightChanged: UI.ui.updateHudOccluder(tabBar)
        onXChanged: UI.ui.updateHudOccluder(tabBar)
        onYChanged: UI.ui.updateHudOccluder(tabBar)
    }

    Item {
        id: mainPane
        focus: true
        width: 600
        height: Math.max(600, 0.67 * parent.height)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter

        StackView {
            id: stackView
            anchors.fill: parent
            anchors.margins: 16
            initialItem: generalComponent
            clip: true
        }

        Component.onCompleted: UI.ui.registerHudOccluder(mainPane)
        Component.onDestruction: UI.ui.unregisterHudOccluder(mainPane)
        onWidthChanged: UI.ui.updateHudOccluder(mainPane)
        onHeightChanged: UI.ui.updateHudOccluder(mainPane)
        onXChanged: UI.ui.updateHudOccluder(mainPane)
        onYChanged: UI.ui.updateHudOccluder(mainPane)
    }

    Component {
        id: generalComponent
        Item {
            ColumnLayout {
                spacing: 20
                width: parent.width - 64 - 192
                anchors.centerIn: parent

                InGameButton {
                    text: "Main menu"
                    Layout.fillWidth: true
                    onClicked: UI.ui.showMainMenu()
                }
                InGameButton {
                    visible: canShowLoadouts
                    text: canShowLoadouts ? UI.gametypeOptionsModel.tabTitle : ""
                    Layout.fillWidth: true
                    onClicked: stackView.push(gametypeOptionsComponent)
                }
                InGameButton {
                    visible: UI.ui.canBeReady
                    text: UI.ui.isReady ? "Not ready" : "Ready"
                    Layout.fillWidth: true
                    onClicked: {
                        UI.ui.toggleReady()
                        UI.ui.returnFromInGameMenu()
                    }
                }
                InGameButton {
                    visible: UI.ui.canJoin
                    text: "Join"
                    Layout.fillWidth: true
                    onClicked: {
                        if (UI.hudDataModel.hasTwoTeams && UI.ui.canJoinAlpha && UI.ui.canJoinBeta) {
                            stackView.push(teamSelectionComponent)
                        } else {
                            UI.ui.join()
                            UI.ui.returnFromInGameMenu()
                        }
                    }
                }
                InGameButton {
                    visible: !UI.ui.canJoin && (UI.ui.canJoinAlpha !== UI.ui.canJoinBeta)
                    text: "Switch team"
                    Layout.fillWidth: true
                    onClicked: {
                        if (UI.hudDataModel.isInWarmupState) {
                            if (UI.ui.canJoinAlpha) {
                                UI.ui.joinAlpha()
                            } else {
                                UI.ui.joinBeta()
                            }
                            UI.ui.returnFromInGameMenu()
                        } else {
                            stackView.push(switchTeamConfirmationComponent)
                        }
                    }
                }
                InGameButton {
                    visible: UI.ui.canToggleChallengerStatus && UI.hudDataModel.isSpectator
                    text: UI.ui.isInChallengersQueue ? "Leave the queue" : "Enter the queue"
                    Layout.fillWidth: true
                    onClicked: {
                        UI.ui.toggleChallengerStatus()
                        UI.ui.returnFromInGameMenu()
                    }
                }
                InGameButton {
                    visible: UI.ui.canSpectate
                    text: "Spectate"
                    Layout.fillWidth: true
                    onClicked: {
                        if (UI.hudDataModel.isInWarmupState) {
                            UI.ui.spectate()
                            UI.ui.returnFromInGameMenu()
                        } else {
                            stackView.push(spectateConfirmationComponent)
                        }
                    }
                }
                InGameButton {
                    text: "Disconnect"
                    Layout.fillWidth: true
                    onClicked: {
                        if (UI.hudDataModel.isSpectator || UI.hudDataModel.isInWarmupState) {
                            UI.ui.disconnect()
                        } else {
                            stackView.push(disconnectConfirmationComponent)
                        }
                    }
                }
            }
        }
    }

    Component {
        id: chatComponent
        InGameChatPage {}
    }

    Component {
        id: callvotesComponent
        InGameCallvotesPage {}
    }

    Component {
        id: gametypeOptionsComponent
        InGameGametypeOptionsPage {
            Connections {
                target: root
                onCanShowLoadoutsChanged: {
                    if (!root.canShowLoadouts) {
                        stackView.pop()
                    }
                }
            }
        }
    }

    Component {
        id: spectateConfirmationComponent
        InGameConfirmationPage {
            titleText: "Spectate?"
            onAccepted: {
                UI.ui.spectate()
                UI.ui.returnFromInGameMenu()
            }
            onRejected: stackView.pop()

            Connections {
                target: UI.ui
                onCanSpectateChanged: {
                    if (!UI.ui.canSpectate) {
                        stackView.pop()
                    }
                }
            }
            Connections {
                target: UI.hudDataModel
                onIsInPostmatchStateChanged: {
                    if (UI.hudDataModel.isInPostmatchState) {
                        stackView.pop()
                    }
                }
            }
        }
    }

    Component {
        id: switchTeamConfirmationComponent
        InGameConfirmationPage {
            titleText: "Switch team?"
            onAccepted: {
                if (UI.ui.canJoinAlpha) {
                    UI.ui.joinAlpha()
                } else {
                    UI.ui.joinBeta()
                }
                UI.ui.returnFromInGameMenu()
            }
            onRejected: stackView.pop()
            Connections {
                target: UI.ui
                onCanJoinAlphaChanged: {
                    if (UI.ui.canJoinBeta) {
                        stackView.pop()
                    }
                }
                onCanJoinBetaChanged: {
                    if (UI.ui.canJoinAlpha) {
                        stackView.pop()
                    }
                }
            }
            Connections {
                target: UI.hudDataModel
                onIsInPostmatchStateChanged: {
                    if (UI.hudDataModel.isInPostmatchState) {
                        stackView.pop()
                    }
                }
            }
        }
    }

    Component {
        id: disconnectConfirmationComponent
        InGameConfirmationPage {
            titleText: "Disconnect?"
            onAccepted: UI.ui.disconnect()
            onRejected: stackView.pop()
        }
    }

    Component {
        id: teamSelectionComponent
        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 64 - 192
                spacing: 20

                InGameButton {
                    Layout.fillWidth: true
                    text: "Join any team"
                    onClicked: {
                        UI.ui.join()
                        UI.ui.returnFromInGameMenu()
                    }
                }
                InGameButton {
                    text: "Join '" + UI.hudDataModel.alphaName + "'"
                    Layout.fillWidth: true
                    onClicked: {
                        UI.ui.joinAlpha()
                        UI.ui.returnFromInGameMenu()
                    }
                }
                InGameButton {
                    text: "Join '" + UI.hudDataModel.betaName + "'"
                    Layout.fillWidth: true
                    onClicked: {
                        UI.ui.joinBeta()
                        UI.ui.returnFromInGameMenu()
                    }
                }
            }
            Connections {
                target: UI.ui
                onCanJoinAlphaChanged: {
                    if (!UI.ui.canJoinAlpha) {
                        stackView.pop()
                    }
                }
                onCanJoinBetaChanged: {
                    if (!UI.ui.canJoinBeta) {
                        stackView.pop()
                    }
                }
            }
            Connections {
                target: UI.hudDataModel
                onIsInPostmatchStateChanged: {
                    if (UI.hudDataModel.isInPostmatchState) {
                        stackView.pop()
                    }
                }
            }
        }
    }

    Connections {
        target: UI.gametypeOptionsModel
        onAvailableChanged: {
            if (!available) {
                stackView.replace(generalComponent)
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            stackView.forceActiveFocus()
        }
    }

    Keys.onPressed: {
        if (visible) {
            const currentItem = stackView.currentItem
            if (currentItem) {
                // Check if the current item can handle the event on its own
                const handler = currentItem["handleKeyEvent"]
                if (handler && handler(event)) {
                    return
                }
                if (event.key === Qt.Key_Escape) {
                    event.accepted = true
                    // Check if the current item can handle back navigation on its own
                    const handler = currentItem["handleKeyBack"]
                    if (handler && handler(event)) {
                        return
                    }
                    if (stackView.depth < 2) {
                        if (tabBar.currentIndex) {
                            tabBar.currentIndex = 0
                        } else {
                            UI.ui.returnFromInGameMenu()
                        }
                    } else {
                        stackView.pop()
                    }
                }
            }
        }
    }
}