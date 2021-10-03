import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

ListView {
    spacing: 16
    model: actionRequestsModel

    delegate: Item {
        width: parent.width
        height: Math.max(96, contentColumn.implicitHeight + 32)
        Rectangle {
            anchors.fill: parent
            radius: 3
            color: expectsInput ? Material.accent : Qt.lighter(Material.background)
            opacity: 0.8
            layer.enabled: true
            layer.effect: ElevationEffect { elevation: 64 }
        }

        ColumnLayout {
            id: contentColumn
            anchors.centerIn: parent
            width: parent.width
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                horizontalAlignment: Qt.AlignHCenter
                verticalAlignment: Qt.AlignVCenter
                wrapMode: Text.WordWrap
                font.weight: Font.Bold
                font.pointSize: 14
                font.letterSpacing: 2
                textFormat: Text.StyledText
                text: title
            }
            Label {
                visible: desc.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                horizontalAlignment: Qt.AlignHCenter
                verticalAlignment: Qt.AlignVCenter
                wrapMode: Text.WordWrap
                font.weight: Font.Bold
                font.pointSize: 12
                font.letterSpacing: 2
                textFormat: Text.StyledText
                text: desc
            }
        }
    }
}