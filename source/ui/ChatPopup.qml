import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    readonly property color defaultMaterialAccent: Material.accent

    Item {
        id: contentFrame
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 16

        width: Math.min(parent.width, 800)
        height: 220

        layer.enabled: true
        layer.effect: ElevationEffect { elevation: 64 }

        Rectangle {
            anchors.fill: parent
            color: wsw.isShowingTeamChatPopup ? Material.accent : Material.background
            radius: 1
            opacity: 0.7
        }

        TextField {
            id: input
            Material.accent: wsw.isShowingTeamChatPopup ? "white" : defaultMaterialAccent

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 12

            Component.onCompleted: forceActiveFocus()

            onEditingFinished: {
                wsw.sendChatMessage(text, wsw.isShowingTeamChatPopup)
                text = ""
            }
        }

        ListView {
            model: wsw.isShowingTeamChatPopup ? compactTeamChatModel : compactChatModel
            verticalLayoutDirection: ListView.BottomToTop
            spacing: 4
            clip: true
            displayMarginBeginning: 0
            displayMarginEnd: 0
            anchors.top: input.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 8

            onCountChanged: updateHeightAndPosition()
            Component.onCompleted: updateHeightAndPosition()

            // We want to keep items of short lists on the pane top
            // contrary to what ListView.BottomToTop produces.
            // This is achieved by varying the list height itself.
            // We want to avoid computing content height on every update
            // as this is discouraged for long lists.
            // That's why it is implemented in a procedural fashion breaking binding chains.
            function updateHeightAndPosition() {
                const allowedHeight =
                    contentFrame.height - input.height - input.anchors.topMargin -
                    input.anchors.bottomMargin - anchors.topMargin - anchors.bottomMargin
                // Slightly greater than the maximal number that gets really displayed
                if (count < 12) {
                    height = Math.min(allowedHeight, contentHeight)
                } else {
                    height = allowedHeight
                }
                positionViewAtEnd()
            }

            delegate: RowLayout {
                width: parent.width
                Label {
                    Layout.alignment: Qt.AlignTop
                    Layout.rightMargin: 2
                    font.pointSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 0.5
                    textFormat: Text.StyledText
                    style: Text.Raised
                    text: name + ':'
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pointSize: 11
                    font.letterSpacing: 0.5
                    textFormat: Text.StyledText
                    clip: true
                    text: message
                }
            }
        }
    }

    Keys.onPressed: {
        if (event.key == Qt.Key_Escape || event.key == Qt.Key_Back) {
            wsw.closeChatPopup()
        }
    }
}