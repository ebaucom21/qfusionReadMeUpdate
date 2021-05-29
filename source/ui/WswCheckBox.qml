import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

CheckBox {
    Material.theme: checked ? Material.Light : Material.Dark
    Component.onCompleted: {
        contentItem.font.pointSize = 11
        contentItem.font.letterSpacing = 0.5
        contentItem.font.weight = Font.Medium
    }
}
