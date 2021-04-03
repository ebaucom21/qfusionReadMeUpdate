import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Rectangle {
    id: root
    color: Qt.rgba(Material.background.r, Material.background.g, Material.background.b, 0.8)

    // TODO: Share with the in-game menu
    readonly property real heightFrac: (Math.min(1080, rootItem.height - 720)) / (1080 - 720)

    readonly property color baseColor: Qt.rgba(Material.background.r, Material.background.g, Material.background.b, 0.7)
    readonly property color tintColor: Qt.rgba(Material.accent.r, Material.accent.g, Material.accent.b, 0.05)

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

    ScoreboardSpecsPane {
        anchors.top: teamTablesLoader.bottom
        anchors.topMargin: 48
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.tableWidth
        baseColor: "black"
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