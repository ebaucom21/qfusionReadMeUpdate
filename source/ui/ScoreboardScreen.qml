import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Rectangle {
    color: Qt.rgba(Material.background.r, Material.background.g, Material.background.b, 0.8)

    // TODO: Share with the in-game menu
    readonly property real heightFrac: (Math.min(1080, rootItem.height - 720)) / (1080 - 720)

    readonly property color baseColor: Qt.rgba(Material.background.r, Material.background.g, Material.background.b, 0.7)
    readonly property color tintColor: Qt.rgba(Material.accent.r, Material.accent.g, Material.accent.b, 0.05)

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
                baseColor: Material.accent
                model: scoreboardPlayersModel
                Layout.fillWidth: true
            }
            ScoreboardPlayersList {
                baseColor: "red"
                model: scoreboardAlphaModel
                Layout.fillWidth: true
            }
            ScoreboardPlayersList {
                baseColor: "green"
                model: scoreboardBetaModel
                Layout.fillWidth: true
            }
            ScoreboardSpecsList {
                baseColor: "black"
                model: scoreboardSpecsModel
                Layout.fillWidth: true
            }
        }
    }
}