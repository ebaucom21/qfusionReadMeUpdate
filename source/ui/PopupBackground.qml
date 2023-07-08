import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import net.warsow 2.6

Rectangle {
    id: root
    Rectangle {
        width: parent.width
        height: 4
        anchors.top: parent.top
        color: Material.accentColor
    }

    implicitWidth: UI.ui.desiredPopupWidth
    implicitHeight: UI.ui.desiredPopupHeight
    color: Material.backgroundColor
    radius: 3
    layer.enabled: root.enabled
    layer.effect: ElevationEffect { elevation: 64 }
}