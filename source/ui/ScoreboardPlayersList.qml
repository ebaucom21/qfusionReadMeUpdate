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

    property real baseCellWidth
    property real clanCellWidth

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
        readonly property bool isColumnTextual: (kind === Scoreboard.Nickname) || (kind === Scoreboard.Clan)
        readonly property bool isColumnStatusOne: kind === Scoreboard.Status
        readonly property bool isDisplayingGlyph: (kind === Scoreboard.Glyph) || (isColumnStatusOne && value >= 32)
        readonly property bool shouldBeDisplayedAsIcon: (kind === Scoreboard.Icon) || (isColumnStatusOne && value < 32)
        readonly property real valueOpacity: isGhosting ? 0.5 : 1.0

        implicitWidth: kind === Scoreboard.Nickname ?
                       (tableView.width - clanCellWidth - (tableView.columns - 2) * baseCellWidth) :
                       (kind === Scoreboard.Clan ? clanCellWidth : baseCellWidth)

        implicitHeight: 32
        onImplicitHeightChanged: forceLayoutTimer.start()
        onHeightChanged: forceLayoutTimer.start()

        Rectangle {
            anchors.fill: parent
            visible: !isColumnStatusOne
            opacity: 0.7
            color: isColumnStatusOne ? "transparent" : getCellColor(row, column)
        }

        Label {
            visible: !shouldBeDisplayedAsIcon
            opacity: isColumnStatusOne ? 1.0 : valueOpacity
            anchors.fill: parent
            verticalAlignment: Qt.AlignVCenter
            horizontalAlignment: isColumnTextual ? Qt.AlignLeft : Qt.AlignHCenter
            padding: 4
            text: value
            textFormat: Text.StyledText
            font.weight: Font.Medium
            font.pointSize: 12
            font.letterSpacing: 1
            font.strikeout: isGhosting && isColumnTextual
        }

        Loader {
            active: value && shouldBeDisplayedAsIcon
            anchors.centerIn: parent

            Image {
                opacity: valueOpacity
                width: 32
                height: 32
                source: scoreboard.getImageAssetPath(value)
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