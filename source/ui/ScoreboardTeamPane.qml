import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root

    property var model

    property real baseCellWidth
    property real clanCellWidth
    property color baseColor

    property int bufferIndex: 0

    implicitHeight: (front.status === Loader.Ready) ?
                    front.item.implicitHeight :
                    ((back.status === Loader.Ready) ? back.item.implicitHeight : 0)

    // Hacks to force redrawing team tables.
	// (QML TableView does not destroy items immediately and they are visible through newly created transparent ones).
	// Neither forceLayout() nor reuseItems/onPooled/onReused work as expected.
	// TODO: Discover a better solution.

    Component {
        id: component
        ScoreboardTable {
            model: root.model
            baseColor: root.baseColor
            baseCellWidth: root.baseCellWidth
            clanCellWidth: root.clanCellWidth
        }
    }

    Loader {
        id: front
        width: root.width
        active: root.visible && bufferIndex === 0
        sourceComponent: component
    }

    Loader {
        id: back
        width: root.width
        active: root.visible && bufferIndex === 1
        sourceComponent: component
    }

    Connections {
        target: scoreboard
        onTeamReset: {
            if (resetTeamTag === model.teamTag) {
                bufferIndex = (bufferIndex + 1) % 2
            }
        }
    }
}