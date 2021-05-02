import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    width: implicitWidth
    height: implicitHeight

    implicitWidth: 240
    implicitHeight: list.contentHeight + 20

    ListView {
        id: list
        model: hudDataModel.getObituariesModel()
        width: parent.width - 16
        height: contentHeight
        anchors.centerIn: parent
        spacing: 4

        delegate: RowLayout {
            width: list.width
            height: 27
            // Something is wrong with transitions (the don't always complete, wtf?). Use a simpler approach.
            opacity: 0.0
            Component.onCompleted: opacity = 1.0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                horizontalAlignment: Qt.AlignRight
                font.weight: Font.Bold
                font.letterSpacing: 1
                font.pointSize: 12
                style: Text.Raised
                text: attacker
            }
            Image {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                source: iconPath
                smooth: true
                mipmap: true
            }
            Label {
                Layout.alignment: Qt.AlignVCenter
                font.weight: Font.Bold
                font.letterSpacing: 1
                font.pointSize: 12
                style: Text.Raised
                text: victim
            }
        }
    }
}