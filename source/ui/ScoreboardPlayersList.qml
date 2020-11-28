import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

TableView {
    id: tableView
    columnSpacing: 0
    rowSpacing: 0
    reuseItems: false
    interactive: false
    contentHeight: 32 * rows
    implicitHeight: 32 * rows

    property color baseColor

    delegate: Item {
        readonly property int kind: scoreboard.getColumnKind(column)

        implicitWidth: {
            if (kind == Scoreboard.Nickname) {
                tableView.width - 96 - (tableView.columns - 2) * 48
            } else {
                kind == Scoreboard.Clan ? 96 : 48
            }
        }

        implicitHeight: 32
        onImplicitHeightChanged: forceLayoutTimer.start()
        onHeightChanged: forceLayoutTimer.start()

        Rectangle {
            anchors.fill: parent
            opacity: 0.7
            color: row % 2 ?
                Qt.lighter(baseColor, column % 2 ? 1.2 : 1.5) :
                Qt.darker(baseColor, column % 2 ? 1.2 : 1.5)
        }

        Label {
            visible: kind != Scoreboard.Icon
            anchors.fill: parent
            verticalAlignment: Qt.AlignVCenter
            horizontalAlignment: kind == Scoreboard.Nickname || kind == Scoreboard.Clan ? Qt.AlignLeft : Qt.AlignHCenter
            padding: 4
            text: value
            font.weight: Font.Medium
            font.pointSize: 12
            font.letterSpacing: 1
        }

        Loader {
            active: kind == Scoreboard.Icon
            anchors.centerIn: parent

            Image {
                visible: value
                width: 32
                height: 32
                source: value ? scoreboard.getImageAssetPath(value) : ""
            }
        }
    }

    onColumnsChanged: forceLayoutTimer.start()
    onRowsChanged: forceLayoutTimer.start()

    Timer {
        id: forceLayoutTimer
        interval: 1
        onTriggered: tableView.forceLayout()
    }
}