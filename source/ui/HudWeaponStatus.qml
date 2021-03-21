import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import net.warsow 2.6

Item {
    width: implicitWidth
    height: implicitHeight
    implicitWidth: 256
    implicitHeight: 192

    Rectangle {
        id: back
        anchors.centerIn: parent
        radius: 6
        width: 0.80 * parent.width
        height: 0.75 * parent.height
        color: "black"
        opacity: 0.7
        layer.enabled: true
        layer.effect: ElevationEffect { elevation: 64 }
    }

    Label {
        anchors.top: back.top
        anchors.topMargin: 12
        anchors.horizontalCenter: back.horizontalCenter
        font.weight: Font.Medium
        font.letterSpacing: 1
        font.pointSize: 12
        textFormat: Text.PlainText
        text: hudDataModel.activeWeaponName
    }

    Image {
        anchors.right: back.right
        anchors.bottom: back.bottom
        anchors.margins: 8
        width: 96
        height: 96
        smooth: true
        mipmap: true
        source: hudDataModel.activeWeaponIcon
    }

    Label {
        visible: hudDataModel.activeWeaponStrongAmmo && hudDataModel.activeWeaponWeakAmmo
        anchors.left: strongCountLabel.left
        anchors.bottom: strongCountLabel.top
        font.weight: Font.Bold
        font.letterSpacing: 1
        font.pointSize: hudDataModel.activeWeaponWeakAmmo >= 0 ? 13 : 16
        style: Text.Raised
        textFormat: Text.PlainText
        text: hudDataModel.activeWeaponWeakAmmo >= 0 ?
            (hudDataModel.activeWeaponWeakAmmo ? hudDataModel.activeWeaponWeakAmmo : "") : "\u221E"
    }

    Label {
        id: strongCountLabel
        anchors.left: back.left
        anchors.bottom: back.bottom
        anchors.margins: 16
        font.weight: Font.ExtraBold
        font.letterSpacing: 1
        font.pointSize: 28
        style: Text.Raised
        textFormat: Text.PlainText
        text: {
            const ammo = hudDataModel.activeWeaponStrongAmmo || hudDataModel.activeWeaponWeakAmmo
            ammo >= 0 ? (ammo ? ammo : "") : "\u221E"
        }
    }
}