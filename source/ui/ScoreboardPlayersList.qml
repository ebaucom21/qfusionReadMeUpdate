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

    property color baseAlphaColor: "red"
    property color baseBetaColor: "green"

    property bool mixedTeamsMode: false

    function getCellColor(row, column) {
        let c = baseColor
        if (mixedTeamsMode) {
            c = scoreboard.isMixedListRowAlpha(row) ? baseAlphaColor : baseBetaColor
        }
        if (row % 2) {
            return column % 2 ? Qt.darker(c, 1.1) : c
        }
        return column % 2 ? c : Qt.lighter(c, 1.2)
    }

    delegate: Item {
        readonly property int kind: scoreboard.getColumnKind(column)
        readonly property bool isTextual : kind == Scoreboard.Nickname || kind == Scoreboard.Clan
        readonly property real valueOpacity: isGhosting ? 0.5 : 1.0

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
            color: getCellColor(row, column)
        }

        Label {
            visible: kind != Scoreboard.Icon
            opacity: valueOpacity
            anchors.fill: parent
            verticalAlignment: Qt.AlignVCenter
            horizontalAlignment: isTextual ? Qt.AlignLeft : Qt.AlignHCenter
            padding: 4
            text: value
            font.weight: Font.Medium
            font.pointSize: 12
            font.letterSpacing: 1
            font.strikeout: isGhosting && isTextual
        }

        Loader {
            active: kind == Scoreboard.Icon
            anchors.centerIn: parent

            Image {
                visible: value
                opacity: valueOpacity
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