import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root
    property var model

    implicitHeight: model ?
        (label.implicitHeight + label.anchors.topMargin + flow.implicitHeight + flow.anchors.topMargin) : 0

    ServerBrowserDataLabel {
        id: label
        anchors.top: parent.top
        anchors.topMargin: 12
        font.weight: Font.Bold
        anchors.horizontalCenter: parent.horizontalCenter
        text: "Spectators"
    }

    Flow {
        id: flow
        spacing: 12

        anchors.top: label.bottom
        anchors.topMargin: 12
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: 8

        Repeater {
            id: spectatorsRepeater
            model: root.model
            delegate: Row {
                spacing: 8
                ServerBrowserDataLabel {
                    text: modelData["name"]
                }
                ServerBrowserDataLabel {
                    text: wsw.formatPing(modelData["ping"])
                }
            }
        }
    }
}