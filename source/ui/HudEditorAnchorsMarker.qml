import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Rectangle {
    property int displayedAnchors
    property bool highlighted
    visible: displayedAnchors
    color: highlighted ? Material.accent : "pink"
    width: 4
    height: 4
    anchors.centerIn: parent
    anchors.horizontalCenterOffset:
        displayedAnchors & HudLayoutModel.Left ?
            (-parent.width / 2) :
            (displayedAnchors & HudLayoutModel.Right ? parent.width / 2 : 0)
    anchors.verticalCenterOffset:
        displayedAnchors & HudLayoutModel.Top ?
            (-parent.height / 2) :
            (displayedAnchors & HudLayoutModel.Bottom ? parent.height / 2 : 0)
}