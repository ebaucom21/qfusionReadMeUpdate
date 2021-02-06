import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

ListView {
    id: root
    spacing: 8

    delegate: Item {
        width: root.width
        height: Math.max(avatar.height + 8, nameLabel.implicitHeight + contentLabel.implicitHeight + 3 * 8)

        Rectangle {
            id: avatar
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.topMargin: 8
            width: 24
            height: 24
            radius: 12
            opacity: 0.7
            color: "grey"
        }

        Label {
            id: nameLabel
            anchors.top: parent.top
            anchors.left: avatar.right
            anchors.margins: 8
            font.weight: Font.Bold
            font.letterSpacing: 1
            font.pointSize: 12
            textFormat: Text.StyledText
            style: Text.Raised
            text: name
        }

        Label {
            id: timestampLabel
            anchors.left: nameLabel.right
            anchors.baseline: nameLabel.baseline
            anchors.margins: 8
            font.letterSpacing: 0
            font.pointSize: 10
            textFormat: Text.PlainText
            text: timestamp
            opacity: 0.7
        }

        Label {
            id: contentLabel
            anchors.left: avatar.right
            anchors.right: parent.right
            anchors.top: nameLabel.bottom
            anchors.margins: 8
            horizontalAlignment: Qt.AlignLeft
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
            font.weight: Font.Normal
            font.letterSpacing: 1.2
            font.pointSize: 11
            lineHeight: 1.2
            clip: true
            text: message
        }
    }
}