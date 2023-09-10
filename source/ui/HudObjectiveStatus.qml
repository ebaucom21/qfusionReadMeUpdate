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

    // TODO: Use a single model (QAbstractItemModel) for all indicators?
    readonly property bool idle: !indicator1.indicatorProgress &&
        !indicator2.indicatorProgress && !indicator3.indicatorProgress

    readonly property int numEnabledIndicators:
        (Hud.dataModel.indicator1State.enabled ? 1 : 0) +
        (Hud.dataModel.indicator2State.enabled ? 1 : 0) +
        (Hud.dataModel.indicator3State.enabled ? 1 : 0)

    property real barHeightFrac: idle ? 0.0 : 1.0
    Behavior on barHeightFrac { SmoothedAnimation { duration: 500 } }

    Rectangle {
        anchors.fill: parent
        // Don't leave the gap between team score elements even if all indicators are hidden
        color: (row.width || Hud.dataModel.hasTwoTeams) ? Qt.rgba(0.0, 0.0, 0.0, 0.6) : "transparent"
        radius: 1

        layer.enabled: row.width
        layer.effect: ElevationEffect { elevation: 16 }
        Component.onDestruction: Hud.destroyLayer(layer)
    }

    Row {
        id: row
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter

        HudObjectiveIndicator {
            id: indicator1
            useExclusiveMode: numEnabledIndicators === 1 && indicator1.indicatorState.enabled && indicator1.canUseExclusiveMode
            barHeightFrac: root.barHeightFrac
            indicatorState: Hud.dataModel.indicator1State
        }
        HudObjectiveIndicator {
            id: indicator2
            useExclusiveMode: numEnabledIndicators === 1 && indicator2.indicatorState.enabled && indicator2.canUseExclusiveMode
            barHeightFrac: root.barHeightFrac
            indicatorState: Hud.dataModel.indicator2State
        }
        HudObjectiveIndicator {
            id: indicator3
            useExclusiveMode: numEnabledIndicators === 1 && indicator3.indicatorState.enabled && indicator3.canUseExclusiveMode
            barHeightFrac: root.barHeightFrac
            indicatorState: Hud.dataModel.indicator3State
        }
    }
}