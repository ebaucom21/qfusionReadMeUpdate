import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root
    anchors.fill: parent

    Component.onCompleted: {
        inGameHudLayoutModel.load("default")
    }

    Repeater {
        id: repeater
        model: inGameHudLayoutModel

        delegate: HudLayoutItem {
            id: itemLoader
            width: item ? item.implicitWidth : 0
            height: item ? item.implicitHeight : 0

            readonly property var controllingCVar: model.controllingCVar
            property bool isCVarOn: false

            anchors.top: getQmlAnchor(HudLayoutModel.Top)
            anchors.bottom: getQmlAnchor(HudLayoutModel.Bottom)
            anchors.left: getQmlAnchor(HudLayoutModel.Left)
            anchors.right: getQmlAnchor(HudLayoutModel.Right)
            anchors.horizontalCenter: getQmlAnchor(HudLayoutModel.HCenter)
            anchors.verticalCenter: getQmlAnchor(HudLayoutModel.VCenter)

            Connections {
                target: wsw
                onHudOccludersChanged: itemLoader.updateItemVisibility()
                onIsShowingPovHudChanged: itemLoader.updateItemVisibility()
            }

            Connections {
                target: hudDataModel
                onHasTwoTeamsChanged: itemLoader.updateItemVisibility()
            }

            Connections {
                target: item
                onWidthChanged: itemLoader.updateItemVisibility()
                onHeightChanged: itemLoader.updateItemVisibility()
                onXChanged: itemLoader.updateItemVisibility()
                onYChanged: itemLoader.updateItemVisibility()
            }

            onLoaded: itemLoader.updateItemVisibility()

            Component.onCompleted: {
                if (itemLoader.controllingCVar) {
                    wsw.registerCVarAwareControl(itemLoader)
                    checkCVarChanges()
                }
                updateItemVisibility()
            }

            Component.onDestruction: {
                if (itemLoader.controllingCVar) {
                    wsw.unregisterCVarAwareControl(itemLoader)
                }
            }

            function checkCVarChanges() {
                const wasCVarOn = itemLoader.isCVarOn
                const stringValue = wsw.getCVarValue(controllingCVar)
                const numericValue = parseInt(stringValue, 10)
                itemLoader.isCVarOn = numericValue && !isNaN(numericValue)
                if (wasCVarOn !== itemLoader.isCVarOn) {
                    updateItemVisibility()
                }
            }

            function updateItemVisibility() {
                if (item) {
                    if (itemLoader.controllingCVar && !itemLoader.isCVarOn) {
                        item.visible = false
                    } else if (!hudDataModel.hasTwoTeams && (flags & HudLayoutModel.TeamBasedOnly)) {
                        item.visible = false
                    } else if (!wsw.isShowingPovHud && (flags & HudLayoutModel.PovOnly)) {
                        item.visible = false
                    } else {
                        // Put the expensive test last
                        item.visible = !wsw.isHudItemOccluded(item)
                    }
                }
            }

            sourceComponent: {
                if (kind === HudLayoutModel.HealthBar) {
                    healthBarComponent
                } else if (kind === HudLayoutModel.ArmorBar) {
                    armorBarComponent
                } else if (kind === HudLayoutModel.InventoryBar) {
                    inventoryBarComponent
                } else if (kind === HudLayoutModel.WeaponStatus) {
                    weaponStatusComponent
                } else if (kind === HudLayoutModel.MatchTime) {
                    matchTimeComponent
                } else if (kind === HudLayoutModel.AlphaScore) {
                    alphaScoreComponent
                } else if (kind === HudLayoutModel.BetaScore) {
                    betaScoreComponent
                } else if (kind === HudLayoutModel.Chat) {
                    chatComponent
                } else if (kind === HudLayoutModel.TeamInfo) {
                    teamInfoComponent
                } else if (kind === HudLayoutModel.FragsFeed) {
                    fragsFeedComponent
                } else if (kind === HudLayoutModel.MessageFeed) {
                    messageFeedComponent
                } else {
                    undefined
                }
            }

            Component {
                id: healthBarComponent
                HudValueBar {
                    text: "HEALTH"
                    color: hudDataModel.health > 100 ? "deeppink" :
                                                        (hudDataModel.health >= 50 ? "white" : "orangered")
                    value: hudDataModel.health
                    frac: 0.01 * Math.min(100.0, Math.max(0, hudDataModel.health))
                    iconPath: hudDataModel.health > 100 ? "image://wsw/gfx/hud/icons/health/100" :
                                                          "image://wsw/gfx/hud/icons/health/50"
                }
            }

            Component {
                id: armorBarComponent
                HudValueBar {
                    text: "ARMOR"
                    value: hudDataModel.armor
                    frac: 0.01 * Math.min(100.0, hudDataModel.armor)
                    color: hudDataModel.armor >= 125 ? "red" : (hudDataModel.armor >= 75 ? "gold" : "green")
                    iconPath: {
                        hudDataModel.armor >= 125 ? "image://wsw/gfx/hud/icons/armor/ra" :
                        (hudDataModel.armor >= 75 ? "image://wsw/gfx/hud/icons/armor/ya" :
                                                    "image://wsw/gfx/hud/icons/armor/ga")
                    }
                }
            }

            Component {
                id: inventoryBarComponent
                HudInventoryBar {}
            }

            Component {
                id: weaponStatusComponent
                HudWeaponStatus {}
            }

            Component {
                id: matchTimeComponent
                HudMatchTime {}
            }

            Component {
                id: alphaScoreComponent
                HudTeamScore {
                    visible: hudDataModel.hasTwoTeams
                    leftAligned: true
                    color: hudDataModel.alphaColor
                    name: hudDataModel.alphaName
                    score: hudDataModel.alphaScore
                    teamStatus: hudDataModel.alphaTeamStatus
                    playersStatus: hudDataModel.alphaPlayersStatus
                }
            }

            Component {
                id: betaScoreComponent
                HudTeamScore {
                    visible: hudDataModel.hasTwoTeams
                    leftAligned: false
                    color: hudDataModel.betaColor
                    name: hudDataModel.betaName
                    score: hudDataModel.betaScore
                    teamStatus: hudDataModel.betaTeamStatus
                    playersStatus: hudDataModel.betaPlayersStatus
                }
            }

            Component {
                id: chatComponent
                HudChat {}
            }

            Component {
                id: teamInfoComponent
                HudTeamInfo {}
            }

            Component {
                id: fragsFeedComponent
                HudFragsFeed {}
            }

            Component {
                id: messageFeedComponent
                HudMessageFeed {}
            }

            function getQmlAnchor(anchorBit) {
                const anchorItem = anchorItemIndex > 0 ? repeater.itemAt(anchorItemIndex - 1) : root
                return getQmlAnchorOfItem(selfAnchors, anchorItemAnchors, anchorBit, anchorItem)
            }
        }
    }

}