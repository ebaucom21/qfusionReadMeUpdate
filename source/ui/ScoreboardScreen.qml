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

    readonly property real baseCellWidth: 48
    readonly property real clanCellWidth: 96

    Item {
        id: baselinePane
        anchors.centerIn: parent
        // TODO: Share with the in-game menu
        width: 480 + 120 * heightFrac
        height: 560 + 210 * heightFrac

        ColumnLayout {
            id: column
            width: baselinePane.width
            anchors.margins: 16
            spacing: 20

            ScoreboardPlayersList {
                model: scoreboardPlayersModel
                baseColor: Material.accent
                baseCellWidth: root.baseCellWidth
                clanCellWidth: root.clanCellWidth
                Layout.fillWidth: true
                Layout.rightMargin: -root.baseCellWidth
            }
            ScoreboardPlayersList {
                model: scoreboardAlphaModel
                baseColor: "red"
                baseCellWidth: root.baseCellWidth
                clanCellWidth: root.clanCellWidth
                Layout.fillWidth: true
                Layout.rightMargin: -root.baseCellWidth
            }
            ScoreboardPlayersList {
                model: scoreboardBetaModel
                baseColor: "green"
                baseCellWidth: root.baseCellWidth
                clanCellWidth: root.clanCellWidth
                Layout.fillWidth: true
                Layout.rightMargin: -root.baseCellWidth
            }
            ScoreboardSpecsList {
                model: scoreboardSpecsModel
                baseColor: "black"
                Layout.fillWidth: true
            }
        }
    }
}