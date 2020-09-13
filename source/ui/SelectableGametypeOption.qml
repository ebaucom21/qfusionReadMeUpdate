import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

ToolButton {
    // Avoid exposing the compound "icon.source" in case if we change the underlying component
    property string iconPath
    icon.source: iconPath
    Material.theme: checked ? Material.Light : Material.Dark
    display: AbstractButton.TextUnderIcon
    // Prevent resetting checked state/binding
    MouseArea {
        anchors.fill: parent
        enabled: parent.checked
        hoverEnabled: true
    }
}