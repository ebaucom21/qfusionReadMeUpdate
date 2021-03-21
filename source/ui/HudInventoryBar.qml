import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
    implicitWidth: list.contentWidth + 48
    implicitHeight: 64 + 64
    width: implicitWidth
    height: implicitHeight

    ListView {
        id: list
        anchors.centerIn: parent
        width: contentWidth
        height: 64
        model: hudDataModel.getInventoryModel()
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        spacing: 20

        populate: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "width"; from: 0; to: 64; duration: 100
                }
                NumberAnimation {
                    property: "opacity"; from: 0; to: 1; duration: 100
                }
            }
        }

        add: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "width"; from: 0; to: 64; duration: 100
                }
                NumberAnimation {
                    property: "opacity"; from: 0; to: 1; duration: 100
                }
            }
        }

        delegate: Item {
            width: 64
            height: 64

            Rectangle {
                id: frame
                anchors.centerIn: parent
                width: 56
                height: 89
                radius: 12
                color: "black"
                opacity: 0.7
                layer.enabled: true
                layer.effect: ElevationEffect { elevation: 16 }
            }

            Rectangle {
                anchors.centerIn: parent
                width: 56; height: 89
                radius: 12
                color: "transparent"
                border.width: 3
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
                anchors.topMargin: strongAmmoCount >= 0 ? 6 : 3
                anchors.horizontalCenter: parent.horizontalCenter
                font.weight: Font.ExtraBold
                font.pointSize: strongAmmoCount >= 0 ? 13 : 18
                opacity: strongAmmoCount ? 1.0 : 0.5
                textFormat: Text.PlainText
                text: strongAmmoCount >= 0 ? (strongAmmoCount ? strongAmmoCount : "\u2013") : "\u221E"
                style: Text.Raised
            }

            Label {
                anchors.bottom: frame.bottom
                anchors.bottomMargin: weakAmmoCount >= 0 ? 6 : 3
                anchors.horizontalCenter: parent.horizontalCenter
                font.weight: Font.ExtraBold
                font.pointSize: weakAmmoCount >= 0 ? 13 : 18
                opacity: weakAmmoCount ? 1.0 : 0.5
                textFormat: Text.PlainText
                text: weakAmmoCount >= 0 ? (weakAmmoCount ? weakAmmoCount : "\u2013") : "\u221E"
                style: Text.Raised
            }
        }
    }
}