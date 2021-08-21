import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import net.warsow 2.6

Item {
    // Make anchoring to this element possible even if all indicators are hidden
    implicitWidth: row.width ? row.width + 16 : 1
    // TODO: Avoid using magic numbers for the height of the team score element
    implicitHeight: Math.max(row.height + 12, 80)

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
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenterOffset: -4
        spacing: 8

        HudObjectiveIndicator { indicatorState: hudDataModel.indicator1State }
        HudObjectiveIndicator { indicatorState: hudDataModel.indicator2State }
        HudObjectiveIndicator { indicatorState: hudDataModel.indicator3State }
    }
}