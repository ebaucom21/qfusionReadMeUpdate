import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Rectangle {
    id: root

    color: wsw.colorWithAlpha(Material.background, wsw.fullscreenOverlayOpacity)

    readonly property real baseCellWidth: 64
    readonly property real clanCellWidth: 96

    readonly property real tableWidth: 600

    // A column layout could have been more apporpriate but it lacks hoirzontal center offset properties

    Loader {
        id: teamTablesLoader
        anchors.top: parent.top
        anchors.topMargin: 0.25 * root.height
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: 0.5 * root.baseCellWidth
        sourceComponent: {
            if (!hudDataModel.hasTwoTeams) {
                playersDisplayComponent
            } else if (scoreboard.display === Scoreboard.Mixed) {
                mixedTeamsDisplayComponent
            } else if (scoreboard.display === Scoreboard.SideBySide && root.width > 2 * root.tableWidth) {
                sideBySideTeamsDisplayComponent
            } else {
                columnWiseTeamsDisplayComponent
            }
        }
    }

    Item {
        id: hudOccluder
        anchors.top: teamTablesLoader.top
        anchors.horizontalCenter: teamTablesLoader.horizontalCenter
        width: Math.max(teamTablesLoader.item.width, specsPane.width)
        height: teamTablesLoader.item.height +
            (challengersPane.visible ? challengersPane.anchors.topMargin + challengersPane.height : 0) +
            (specsPane.visible ? specsPane.anchors.topMargin + specsPane.height : 0) +
            (chasersPane.visible ? chasersPane.anchors.topMargin + chasersPane.height : 0)

        Component.onCompleted: wsw.registerHudOccluder(hudOccluder)
        Component.onDestruction: wsw.unregisterHudOccluder(hudOccluder)
        onWidthChanged: wsw.updateHudOccluder(hudOccluder)
        onHeightChanged: wsw.updateHudOccluder(hudOccluder)
        onXChanged: wsw.updateHudOccluder(hudOccluder)
        onYChanged: wsw.updateHudOccluder(hudOccluder)
    }

    ScoreboardSpecsPane {
        id: challengersPane
        playersPerRow: 3
        playersInFirstRow: 2
        anchors.top: teamTablesLoader.bottom
        anchors.topMargin: 48
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.tableWidth - root.baseCellWidth
        height: implicitHeight
        model: scoreboard.challengersModel
        title: "Challengers"
    }

    ScoreboardSpecsPane {
        id: specsPane
        playersPerRow: 3
        playersInFirstRow: 3
        anchors.top: challengersPane.bottom
        anchors.topMargin: 48
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.tableWidth - root.baseCellWidth
        height: implicitHeight
        model: scoreboard.specsModel
        title: "Spectators"
    }

    ScoreboardSpecsPane {
        id: chasersPane
        playersPerRow: 3
        playersInFirstRow: 3
        visible: scoreboard.hasChasers && scoreboard.chasersModel.length
        anchors.top: specsPane.bottom
        anchors.topMargin: 48
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.tableWidth - root.baseCellWidth
        height: implicitHeight
        model: scoreboard.chasersModel
        title: "Chasers"
    }

    Component {
        id: playersDisplayComponent

        ScoreboardTeamPane {
            width: root.tableWidth
            model: scoreboardPlayersModel
            baseColor: Qt.lighter(Material.background)
            baseCellWidth: root.baseCellWidth
            clanCellWidth: root.clanCellWidth
        }
    }

    Component {
        id: sideBySideTeamsDisplayComponent

        Row {
            ScoreboardTeamPane {
                width: root.tableWidth
                model: scoreboardAlphaModel
                baseColor: Qt.darker(hudDataModel.alphaColor)
                baseCellWidth: root.baseCellWidth
                clanCellWidth: root.clanCellWidth
            }
            ScoreboardTeamPane {
                width: root.tableWidth
                model: scoreboardBetaModel
                baseColor: Qt.darker(hudDataModel.betaColor)
                baseCellWidth: root.baseCellWidth
                clanCellWidth: root.clanCellWidth
            }
        }
    }

    Component {
        id: columnWiseTeamsDisplayComponent

        Column {
            spacing: 32
            ScoreboardTeamPane {
                width: root.tableWidth
                model: scoreboardAlphaModel
                baseColor: Qt.darker(hudDataModel.alphaColor)
                baseCellWidth: root.baseCellWidth
                clanCellWidth: root.clanCellWidth
            }
            ScoreboardTeamPane {
                width: root.tableWidth
                model: scoreboardBetaModel
                displayHeader: false
                baseColor: Qt.darker(hudDataModel.betaColor)
                baseCellWidth: root.baseCellWidth
                clanCellWidth: root.clanCellWidth
            }
        }
    }

    Component {
        id: mixedTeamsDisplayComponent

        ScoreboardTeamPane {
            width: root.tableWidth
            model: scoreboardMixedModel
            mixedTeamsMode: true
            baseAlphaColor: Qt.darker(hudDataModel.alphaColor)
            baseBetaColor: Qt.darker(hudDataModel.betaColor)
            baseCellWidth: root.baseCellWidth
            clanCellWidth: root.clanCellWidth
        }
    }
}