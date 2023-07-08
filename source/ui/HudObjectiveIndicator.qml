import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root

    visible: indicatorState.enabled
    implicitWidth: collapsedHeight
    implicitHeight: collapsedHeight + barHeight + 1.5 * barMargin

    property var indicatorState
    property real barHeightFrac

    readonly property color indicatorColor: indicatorState.color
    readonly property int indicatorProgress: indicatorState.progress
    readonly property int indicatorAnim: indicatorState.anim

    readonly property real minFrameBaseOpacity: 0.5
    readonly property real maxFrameSide: 72 - 8
    readonly property real collapsedHeight: 80
    readonly property real borderWidth: 5

    readonly property real barHeight: 16 * barHeightFrac
    readonly property real barMargin: 8 * barHeightFrac

    Item {
        id: frameArea
        anchors.top: parent.top
        anchors.topMargin: -2
        anchors.horizontalCenter: parent.horizontalCenter
        width: collapsedHeight
        height: collapsedHeight
    }

    Rectangle {
        id: frame
        property real baseOpacity: minFrameBaseOpacity
        anchors.centerIn: frameArea
        width: maxFrameSide
        height: maxFrameSide
        color: "transparent"
        border.color: indicatorColor
        border.width: borderWidth
        opacity: Math.min(1.0, baseOpacity + (Hud.ui.isShowingScoreboard ? 0.0 : 0.1))
        radius: 24

        function restoreDefaultProperties() {
            if (!frameAlertAnim.running && !frameActionAnim.running) {
                frame.width = maxFrameSide
                frame.height = maxFrameSide
                frame.baseOpacity = minFrameBaseOpacity
            }
        }
    }

    Image {
        id: icon
        anchors.horizontalCenter: frame.horizontalCenter
        anchors.verticalCenter: frame.verticalCenter
        width: 32
        height: 32
        smooth: true
        mipmap: true
        source: Hud.dataModel.getIndicatorIconPath(indicatorState.iconNum)

        function restoreDefaultProperties() {
            if (!iconAlertAnim.running) {
                icon.anchors.horizontalCenterOffset = 0
            }
            if (!iconActionAnim.running) {
                icon.anchors.verticalCenterOffset = 0
            }
        }
    }

    // Changing anim duration on the fly does not seem to work, declare mutually exclusive instances as a workaround

    HudObjectiveIndicatorIconAnim {
        id: iconAlertAnim
        target: icon
        amplitude: 1.25
        period: 300
        running: indicatorAnim === HudDataModel.AlertAnim
        targetProperty: "anchors.horizontalCenterOffset"
        onRunningChanged: icon.restoreDefaultProperties()
    }

    HudObjectiveIndicatorIconAnim {
        id: iconActionAnim
        target: icon
        amplitude: 2.0
        period: 667
        running: indicatorAnim === HudDataModel.ActionAnim
        targetProperty: "anchors.verticalCenterOffset"
        onRunningChanged: icon.restoreDefaultProperties()
    }

    HudObjectiveIndicatorFrameAnim {
        id: frameAlertAnim
        target: frame
        running: indicatorAnim === HudDataModel.AlertAnim
        minSide: maxFrameSide - 6
        maxSide: maxFrameSide
        minOpacity: minFrameBaseOpacity
        step1Duration: 200
        step2Duration: 67
        onRunningChanged: frame.restoreDefaultProperties()
    }

    HudObjectiveIndicatorFrameAnim {
        id: frameActionAnim
        target: frame
        running: indicatorAnim === HudDataModel.ActionAnim
        minSide: maxFrameSide - 4
        maxSide: maxFrameSide
        minOpacity: minFrameBaseOpacity
        step1Duration: 333
        step2Duration: 96
        onRunningChanged: frame.restoreDefaultProperties()
    }

    Rectangle {
        id: bar
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.barMargin
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 2 * root.barMargin
        height: root.barHeight
        color: Hud.ui.colorWithAlpha(indicatorColor, 0.6)

        Rectangle {
            visible: indicatorProgress
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            width: parent.width * 0.01 * Math.abs(indicatorProgress)
            height: parent.height
            color: indicatorColor
            Behavior on width { SmoothedAnimation { duration: 67 } }
        }
    }
}