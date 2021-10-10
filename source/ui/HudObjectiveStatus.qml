import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import net.warsow 2.6

Item {
    id: root
    // Make anchoring to this element possible even if all indicators are hidden
    implicitWidth: row.width ? row.width : 1
    // TODO: Avoid using magic numbers for the height of the team score element
    implicitHeight: Math.max(row.height, 80)

    readonly property bool idle: !indicator1.indicatorProgress &&
        !indicator2.indicatorProgress && !indicator3.indicatorProgress

    property real barHeightFrac: idle ? 0.0 : 1.0
    Behavior on barHeightFrac { SmoothedAnimation { duration: 500 } }

    Rectangle {
        anchors.fill: parent
        // Don't leave the gap between team score elements even if all indicators are hidden
        color: (row.width || hudDataModel.hasTwoTeams) ? Qt.rgba(0.0, 0.0, 0.0, 0.6) : "transparent"
        radius: 1

        layer.enabled: row.width
        layer.effect: ElevationEffect { elevation: 16 }
    }

    Row {
        id: row
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter

        HudObjectiveIndicator {
            id: indicator1
            barHeightFrac: root.barHeightFrac
            indicatorState: hudDataModel.indicator1State
        }
        HudObjectiveIndicator {
            id: indicator2
            barHeightFrac: root.barHeightFrac
            indicatorState: hudDataModel.indicator2State
        }
        HudObjectiveIndicator {
            id: indicator3
            barHeightFrac: root.barHeightFrac
            indicatorState: hudDataModel.indicator3State
        }
    }
}