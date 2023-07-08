import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: hudField
    anchors.fill: parent

    property real alphaNameWidth
    property real betaNameWidth
    property var model

    Repeater {
        id: repeater
        model: hudField.model

        property int numInstantiatedItems: 0

        delegate: HudLayoutItem {
            id: itemLoader
            width: item ? item.implicitWidth : 0
            height: item ? item.implicitHeight : 0

            readonly property int individualMask: model.individualMask

            states: [
                State {
                    name: "arranged"
                    when: repeater.numInstantiatedItems === repeater.count
                    AnchorChanges {
                        target: itemLoader
                        anchors.top: getQmlAnchor(HudLayoutModel.Top)
                        anchors.bottom: getQmlAnchor(HudLayoutModel.Bottom)
                        anchors.left: getQmlAnchor(HudLayoutModel.Left)
                        anchors.right: getQmlAnchor(HudLayoutModel.Right)
                        anchors.horizontalCenter: getQmlAnchor(HudLayoutModel.HCenter)
                        anchors.verticalCenter: getQmlAnchor(HudLayoutModel.VCenter)
                    }
                }
            ]

            Connections {
                target: Hud.ui
                onHudOccludersChanged: itemLoader.updateItemVisibility()
            }

            Connections {
                target: Hud.dataModel
                onHasTwoTeamsChanged: itemLoader.updateItemVisibility()
                onHasActivePovChanged: itemLoader.updateItemVisibility()
                onIsPovAliveChanged: itemLoader.updateItemVisibility()
                onIsInPostmatchStateChanged: itemLoader.updateItemVisibility()
                onActiveItemsMaskChanged: itemLoader.updateItemVisibility()
            }

            Connections {
                target: item
                onWidthChanged: itemLoader.updateItemVisibility()
                onHeightChanged: itemLoader.updateItemVisibility()
                onXChanged: itemLoader.updateItemVisibility()
                onYChanged: itemLoader.updateItemVisibility()
            }

            onLoaded: itemLoader.updateItemVisibility()
            // onStateChanged gets called too early so it turned to be useless it this regard.
            // Intercepting anchor changes produces desired results.
            anchors.onLeftChanged: itemLoader.updateItemVisibility()
            anchors.onHorizontalCenterChanged: itemLoader.updateItemVisibility()
            anchors.onRightChanged: itemLoader.updateItemVisibility()
            anchors.onTopChanged: itemLoader.updateItemVisibility()
            anchors.onVerticalCenterChanged: itemLoader.updateItemVisibility()
            anchors.onBottomChanged: itemLoader.updateItemVisibility()

            Component.onCompleted: {
                updateItemVisibility()
                repeater.numInstantiatedItems++;
            }

            Component.onDestruction: {
                repeater.numInstantiatedItems--;
            }

            function updateItemVisibility() {
                if (item) {
                    if (itemLoader.individualMask && !(itemLoader.individualMask & Hud.dataModel.activeItemsMask)) {
                        item.visible = false
                    } else if (!Hud.dataModel.hasTwoTeams && (flags & HudLayoutModel.TeamBasedOnly)) {
                        item.visible = false
                    } else if (!Hud.dataModel.hasActivePov && (flags & HudLayoutModel.PovOnly)) {
                        item.visible = false
                    } else if (!Hud.dataModel.isPovAlive && (flags & HudLayoutModel.AlivePovOnly)) {
                        item.visible = false
                    } else if (Hud.dataModel.isInPostmatchState && !(flags & HudLayoutModel.AllowPostmatch)){
                        item.visible = false
                    } else {
                        // Put the expensive test last
                        item.visible = !Hud.ui.isHudItemOccluded(item)
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
                } else if (kind === HudLayoutModel.AwardsArea) {
                    awardsAreaComponent
                } else if (kind === HudLayoutModel.StatusMessage) {
                    statusMessageComponent
                } else if (kind === HudLayoutModel.ObjectiveStatus) {
                    objectiveStatusComponent
                } else {
                    undefined
                }
            }

            Component {
                id: healthBarComponent
                HudValueBar {
                    text: "HEALTH"
                    color: Hud.dataModel.health > 100 ? "deeppink" :
                                                        (Hud.dataModel.health >= 50 ? "white" : "orangered")
                    value: Hud.dataModel.health
                    frac: 0.01 * Math.min(100.0, Math.max(0, Hud.dataModel.health))
                    iconPath: Hud.dataModel.health > 100 ? "image://wsw/gfx/hud/icons/health/100" :
                                                          "image://wsw/gfx/hud/icons/health/50"
                }
            }

            Component {
                id: armorBarComponent
                HudValueBar {
                    text: "ARMOR"
                    value: Hud.dataModel.armor
                    frac: 0.01 * Math.min(100.0, Hud.dataModel.armor)
                    color: Hud.dataModel.armor >= 125 ? "red" : (Hud.dataModel.armor >= 75 ? "gold" : "green")
                    iconPath: {
                        Hud.dataModel.armor >= 125 ? "image://wsw/gfx/hud/icons/armor/ra" :
                        (Hud.dataModel.armor >= 75 ? "image://wsw/gfx/hud/icons/armor/ya" :
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
                    visible: Hud.dataModel.hasTwoTeams
                    leftAligned: true
                    color: Hud.dataModel.alphaColor
                    name: Hud.dataModel.alphaName
                    clan: Hud.dataModel.alphaClan
                    score: Hud.dataModel.alphaScore
                    playersStatus: Hud.dataModel.alphaPlayersStatus
                    siblingNameWidth: hudField.betaNameWidth
                    onNameWidthChanged: hudField.alphaNameWidth = nameWidth
                }
            }

            Component {
                id: betaScoreComponent
                HudTeamScore {
                    visible: Hud.dataModel.hasTwoTeams
                    leftAligned: false
                    color: Hud.dataModel.betaColor
                    name: Hud.dataModel.betaName
                    clan: Hud.dataModel.betaClan
                    score: Hud.dataModel.betaScore
                    playersStatus: Hud.dataModel.betaPlayersStatus
                    siblingNameWidth: hudField.alphaNameWidth
                    onNameWidthChanged: hudField.betaNameWidth = nameWidth
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

            Component {
                id: awardsAreaComponent
                HudAwardsArea {}
            }

            Component {
                id: statusMessageComponent
                HudStatusMessage {}
            }

            Component {
                id: objectiveStatusComponent
                HudObjectiveStatus {}
            }

            function getQmlAnchor(anchorBit) {
                const anchorItem = anchorItemIndex > 0 ? repeater.itemAt(anchorItemIndex - 1) : hudField
                return getQmlAnchorOfItem(selfAnchors, anchorItemAnchors, anchorBit, anchorItem)
            }
        }
    }

}