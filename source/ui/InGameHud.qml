import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: hudField
    anchors.fill: parent

    property real alphaNameWidth
    property real betaNameWidth

    property var layoutModel
    property var commonDataModel
    property var povDataModel

    Repeater {
        id: repeater
        model: hudField.layoutModel

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
                target: hudField.commonDataModel
                onHasTwoTeamsChanged: itemLoader.updateItemVisibility()
                onIsInPostmatchStateChanged: itemLoader.updateItemVisibility()
                onActiveItemsMaskChanged: itemLoader.updateItemVisibility()
            }

            Connections {
                target: hudField.povDataModel
                onHasActivePovChanged: itemLoader.updateItemVisibility()
                onIsPovAliveChanged: itemLoader.updateItemVisibility()
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
                Hud.ui.ensureObjectDestruction(model)
                Hud.ui.ensureObjectDestruction(itemLoader)
            }

            function updateItemVisibility() {
                if (item) {
                    if (itemLoader.individualMask && !(itemLoader.individualMask & hudField.commonDataModel.activeItemsMask)) {
                        item.visible = false
                    } else if (!hudField.commonDataModel.hasTwoTeams && (flags & HudLayoutModel.TeamBasedOnly)) {
                        item.visible = false
                    } else if (!hudField.povDataModel.hasActivePov && (flags & HudLayoutModel.PovOnly)) {
                        item.visible = false
                    } else if (!hudField.povDataModel.isPovAlive && (flags & HudLayoutModel.AlivePovOnly)) {
                        item.visible = false
                    } else if (hudField.commonDataModel.isInPostmatchState && !(flags & HudLayoutModel.AllowPostmatch)){
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
                    color: hudField.povDataModel.health > 100 ? "deeppink" :
                                                                (hudField.povDataModel.health >= 50 ? "white" : "orangered")
                    value: hudField.povDataModel.health
                    frac: 0.01 * Math.min(100.0, Math.max(0, hudField.povDataModel.health))
                    iconPath: hudField.povDataModel.health > 100 ? "image://wsw/gfx/hud/icons/health/100" :
                                                                   "image://wsw/gfx/hud/icons/health/50"
                }
            }

            Component {
                id: armorBarComponent
                HudValueBar {
                    text: "ARMOR"
                    value: hudField.povDataModel.armor
                    frac: 0.01 * Math.min(100.0, hudField.povDataModel.armor)
                    color: hudField.povDataModel.armor >= 125 ? "red" : (hudField.povDataModel.armor >= 75 ? "gold" : "green")
                    iconPath: {
                        hudField.povDataModel.armor >= 125 ? "image://wsw/gfx/hud/icons/armor/ra" :
                        (hudField.povDataModel.armor >= 75 ? "image://wsw/gfx/hud/icons/armor/ya" :
                                                             "image://wsw/gfx/hud/icons/armor/ga")
                    }
                }
            }

            Component {
                id: inventoryBarComponent
                HudInventoryBar {
                    povDataModel: hudField.povDataModel
                }
            }

            Component {
                id: weaponStatusComponent
                HudWeaponStatus {
                    povDataModel: hudField.povDataModel
                }
            }

            Component {
                id: matchTimeComponent
                HudMatchTime {
                    commonDataModel: hudField.commonDataModel
                }
            }

            Component {
                id: alphaScoreComponent
                HudTeamScore {
                    visible: commonDataModel.hasTwoTeams
                    leftAligned: true
                    color: hudField.commonDataModel.alphaColor
                    name: hudField.commonDataModel.alphaName
                    clan: hudField.commonDataModel.alphaClan
                    score: hudField.commonDataModel.alphaScore
                    playersStatus: hudField.commonDataModel.alphaPlayersStatus
                    siblingNameWidth: hudField.betaNameWidth
                    onNameWidthChanged: hudField.alphaNameWidth = nameWidth
                }
            }

            Component {
                id: betaScoreComponent
                HudTeamScore {
                    visible: commonDataModel.hasTwoTeams
                    leftAligned: false
                    color: hudField.commonDataModel.betaColor
                    name: hudField.commonDataModel.betaName
                    clan: hudField.commonDataModel.betaClan
                    score: hudField.commonDataModel.betaScore
                    playersStatus: hudField.commonDataModel.betaPlayersStatus
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
                HudTeamInfo {
                    commonDataModel: hudField.commonDataModel
                    povDataModel: hudField.povDataModel
                }
            }

            Component {
                id: fragsFeedComponent
                HudFragsFeed {
                    commonDataModel: hudField.commonDataModel
                }
            }

            Component {
                id: messageFeedComponent
                HudMessageFeed {
                    povDataModel: hudField.povDataModel
                }
            }

            Component {
                id: awardsAreaComponent
                HudAwardsArea {
                    povDataModel: hudField.povDataModel
                }
            }

            Component {
                id: statusMessageComponent
                HudStatusMessage {
                    povDataModel: hudField.povDataModel
                }
            }

            Component {
                id: objectiveStatusComponent
                HudObjectiveStatus {
                    commonDataModel: hudField.commonDataModel
                }
            }

            function getQmlAnchor(anchorBit) {
                const anchorItem = anchorItemIndex > 0 ? repeater.itemAt(anchorItemIndex - 1) : hudField
                return getQmlAnchorOfItem(selfAnchors, anchorItemAnchors, anchorBit, anchorItem)
            }
        }
    }

}