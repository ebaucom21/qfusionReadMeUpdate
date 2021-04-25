import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root
    property string title
    property var model
    property color baseColor: "black"
    visible: repeater.count > 0
    implicitHeight: repeater.count ?
        (label.anchors.topMargin + label.implicitHeight + label.anchors.topMargin +
        flow.anchors.topMargin + flow.implicitHeight + flow.anchors.bottomMargin + 4) : 0

    Rectangle {
        anchors.fill: parent
        color: baseColor
        opacity: 0.5
    }

    Label {
        id: label
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.75
        font.pointSize: 12
        font.weight: Font.Bold
        text: root.title
        style: Text.Raised
    }

    Flow {
        id: flow
        width: parent.width - 8
        anchors.top: label.bottom
        anchors.topMargin: 16 + 2
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 16

        Repeater {
            id: repeater
            model: root.model
            delegate: Row {
                Label {
                    text: name
                    width: implicitWidth + 8
                    horizontalAlignment: Qt.AlignLeft
                    font.weight: Font.Bold
                    font.pointSize: 12
                    font.letterSpacing: 1
                    style: Text.Raised
                }
                Label {
                    text: ping
                    width: implicitWidth + 12
                    horizontalAlignment: Qt.AlignLeft
                    font.weight: Font.Bold
                    font.pointSize: 12
                    font.letterSpacing: 1
                    style: Text.Raised
                }
            }
        }
    }
}