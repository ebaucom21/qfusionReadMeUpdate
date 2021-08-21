import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root

    visible: indicatorState.enabled
    implicitWidth: maxFrameAreaSide
    implicitHeight: 104

    property var indicatorState

    readonly property color indicatorColor: indicatorState.color
    readonly property int indicatorProgress: indicatorState.progress
    readonly property int indicatorAnim: indicatorState.anim

    readonly property real minFrameAreaBaseOpacity: 0.5
    readonly property real maxFrameAreaSide: 72

    Rectangle {
        id: frameArea
        property real baseOpacity: minFrameAreaBaseOpacity
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: maxFrameAreaSide - height
        width: maxFrameAreaSide
        height: maxFrameAreaSide
        color: "transparent"
        border.color: indicatorColor
        border.width: 7
        opacity: Math.min(1.0, baseOpacity + (wsw.isShowingScoreboard ? 0.0 : 0.1))
        radius: 24

        function restoreDefaultProperties() {
            if (!alertAnim.running && !actionAnim.running) {
                frameArea.width = maxFrameAreaSide
                frameArea.height = maxFrameAreaSide
                frameArea.baseOpacity = minFrameAreaBaseOpacity
            }
        }
    }

    // Changing anim duration on the fly does not seem to work, declare two mutually exclusive instances as a workaround

    HudObjectiveIndicatorFrameAnim {
        id: alertAnim
        running: indicatorAnim === HudDataModel.AlertAnim
        minSide: maxFrameAreaSide - 6
        maxSide: maxFrameAreaSide
        minOpacity: minFrameAreaBaseOpacity
        step1Duration: 200
        step2Duration: 67
        onRunningChanged: frameArea.restoreDefaultProperties()
    }

    HudObjectiveIndicatorFrameAnim {
        id: actionAnim
        running: indicatorAnim === HudDataModel.ActionAnim
        minSide: maxFrameAreaSide - 4
        maxSide: maxFrameAreaSide
        minOpacity: minFrameAreaBaseOpacity
        step1Duration: 333
        step2Duration: 96
        onRunningChanged: frameArea.restoreDefaultProperties()
    }

    Rectangle {
        id: bar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 20
        color: wsw.colorWithAlpha(indicatorColor, 0.6)

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

    Image {
        anchors.horizontalCenter: frameArea.horizontalCenter
        anchors.verticalCenter: frameArea.verticalCenter
        width: 32
        height: 32
        smooth: true
        mipmap: true
        source: hudDataModel.getIndicatorIconPath(indicatorState.iconNum)
    }
}