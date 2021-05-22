import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12

Item {
    id: root

    Layout.fillWidth: true
    property string text
    property real spacing: 32
    property real contentBias: 0.5
    property real extraHeight: 0.0

    implicitHeight: Math.max(label.height, contentItem.height) + extraHeight

    Label {
        id: label
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width * (1.0 - contentBias)
        wrapMode: Text.WordWrap
        maximumLineCount: 99
        horizontalAlignment: Qt.AlignRight
        font.pointSize: 11
        font.weight: Font.Medium
        font.letterSpacing: 0.5
        text: root.text
        color: Material.foreground
        opacity: root.enabled ? 1.0 : 0.5
    }

    default property Item contentItem

    states: [
        State {
            name: "anchored"
            AnchorChanges {
                target: contentItem
                anchors {
                    left: root.left
                    verticalCenter: root.verticalCenter
                }
            }
            PropertyChanges {
                target: contentItem
                anchors.leftMargin: root.spacing + (1.0 - root.contentBias) * root.width
            }
        }
    ]

    Component.onCompleted: {
        contentItem.parent = root
        state = "anchored"
    }
}