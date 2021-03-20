import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root
    anchors.fill: parent

    Component.onCompleted: {
        inGameHudLayoutModel.load("default")
    }

    Repeater {
        id: repeater
        model: inGameHudLayoutModel

        delegate: HudLayoutItem {
            width: 240
            height: 64

            anchors.top: getQmlAnchor(HudLayoutModel.Top)
            anchors.bottom: getQmlAnchor(HudLayoutModel.Bottom)
            anchors.left: getQmlAnchor(HudLayoutModel.Left)
            anchors.right: getQmlAnchor(HudLayoutModel.Right)
            anchors.horizontalCenter: getQmlAnchor(HudLayoutModel.HCenter)
            anchors.verticalCenter: getQmlAnchor(HudLayoutModel.VCenter)

            Rectangle {
                anchors.fill: parent
                radius: 6
                color: if (index % 3 == 0) {
                    Qt.rgba(1.0, 0.0, 0.5, 0.5)
                } else if (index % 3 == 1) {
                    Qt.rgba(0.5, 1.0, 0.0, 0.5)
                } else {
                    Qt.rgba(1.0, 0.0, 1.0, 0.5)
                }
            }

            Label {
                anchors.centerIn: parent
                text: index
                font.weight: Font.Bold
                font.pointSize: 12
            }

            function getQmlAnchor(anchorBit) {
                const anchorItem = anchorItemIndex >= 0 ? repeater.itemAt(anchorItemIndex) : root
                return getQmlAnchorOfItem(selfAnchors, anchorItemAnchors, anchorBit, anchorItem)
            }
        }
    }
}