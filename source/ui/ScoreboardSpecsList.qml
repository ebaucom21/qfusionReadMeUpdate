import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root
    property var model
    property color baseColor
    implicitHeight: repeater.count ? flow.implicitHeight + 16 : 0
    visible: repeater.count > 0

    Rectangle {
        anchors.fill: parent
        color: baseColor
        opacity: 0.7
    }

    Flow {
        id: flow
        width: parent.width - 8
        anchors.centerIn: parent
        spacing: 16

        Repeater {
            id: repeater
            model: root.model
            delegate: Row {
                Label {
                    text: name
                    width: implicitWidth + 8
                    horizontalAlignment: Qt.AlignLeft
                    font.weight: Font.Medium
                    font.pointSize: 12
                    font.letterSpacing: 1
                }
                Label {
                    text: ping
                    width: implicitWidth + 12
                    horizontalAlignment: Qt.AlignLeft
                    font.weight: Font.Medium
                    font.pointSize: 12
                    font.letterSpacing: 1
                }
            }
        }
    }
}