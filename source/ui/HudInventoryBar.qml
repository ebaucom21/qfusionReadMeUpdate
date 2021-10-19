import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
    implicitWidth: list.contentWidth + 48
    implicitHeight: cardHeight + 24
    width: implicitWidth
    height: implicitHeight

    readonly property real cardWidth: 60
    readonly property real cardHeight: 100
    readonly property real animDuration: 100
    readonly property real cardRadius: 9

    ListView {
        id: list
        anchors.centerIn: parent
        width: contentWidth
        height: cardHeight
        model: hudDataModel.getInventoryModel()
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        spacing: 24

        populate: Transition {
            NumberAnimation {
                property: "width"; from: 0; to: cardWidth; duration: animDuration
            }
            NumberAnimation {
                property: "opacity"; from: 0; to: 1; duration: animDuration
            }
        }

        add: Transition {
            NumberAnimation {
                property: "width"; from: 0; to: cardWidth; duration: animDuration
            }
            NumberAnimation {
                property: "opacity"; from: 0; to: 1; duration: animDuration
            }
        }

        displaced: Transition {
            NumberAnimation {
                property: "width"; from: 0; to: cardWidth; duration: animDuration
            }
            NumberAnimation {
                property: "opacity"; from: 0; to: 1; duration: animDuration
            }
        }

        delegate: Item {
            width: cardWidth
            height: cardHeight

            Rectangle {
                id: frame
                anchors.centerIn: parent
                width: cardWidth
                height: cardHeight
                radius: cardRadius
                color: "black"
                opacity: 0.7
                layer.enabled: true
                layer.effect: ElevationEffect { elevation: 16 }
            }

            Rectangle {
                anchors.centerIn: parent
                width: cardWidth + 3; height: cardHeight + 3
                radius: cardRadius - 2
                color: "transparent"
                border.width: 4
                border.color: Qt.lighter(model.color, 1.1)
                visible: active
            }

            Image {
                id: icon
                visible: false
                anchors.centerIn: parent
                width: 32
                height: 32
                source: model.iconPath
                smooth: true
                mipmap: true
            }

            Desaturate {
                anchors.fill: icon
                cached: true
                source: icon
                desaturation: hasWeapon ? 0.0 : 1.0
            }

            Label {
                anchors.top: frame.top
                anchors.topMargin: strongAmmoCount >= 0 ? 6 : 4
                anchors.horizontalCenter: parent.horizontalCenter
                font.family: wsw.numbersFontFamily
                font.weight: Font.Black
                font.pointSize: strongAmmoCount >= 0 ? 14 : 18
                font.letterSpacing: 1.0
                opacity: strongAmmoCount ? 1.0 : 0.5
                textFormat: Text.PlainText
                text: strongAmmoCount >= 0 ? (strongAmmoCount ? strongAmmoCount : "\u2013") : "\u221E"
                style: Text.Raised
            }

            Label {
                anchors.bottom: frame.bottom
                anchors.bottomMargin: weakAmmoCount >= 0 ? 6 : 4
                anchors.horizontalCenter: parent.horizontalCenter
                font.family: wsw.numbersFontFamily
                font.weight: Font.Black
                font.pointSize: weakAmmoCount >= 0 ? 14 : 18
                font.letterSpacing: 1.0
                opacity: weakAmmoCount ? 1.0 : 0.5
                textFormat: Text.PlainText
                text: weakAmmoCount >= 0 ? (weakAmmoCount ? weakAmmoCount : "\u2013") : "\u221E"
                style: Text.Raised
            }
        }
    }
}