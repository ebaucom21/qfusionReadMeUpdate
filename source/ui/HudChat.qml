import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    // Wrap the loader in an item to avoid providing inconsistent implicitHeight in the hidden state

    implicitWidth: 720
    implicitHeight: 192 - 32
    width: implicitWidth
    height: implicitHeight

    readonly property color defaultMaterialAccent: Material.accent

    Loader {
        anchors.fill: parent
        active: wsw.isShowingChatPopup || wsw.isShowingTeamChatPopup
        sourceComponent: chatComponent
    }

    Component {
        id: chatComponent
        Item {
            id: contentFrame
            width: root.width
            height: root.height
            layer.enabled: true
            layer.effect: ElevationEffect { elevation: 64 }

            Rectangle {
                anchors.fill: parent
                color: wsw.isShowingTeamChatPopup ? Material.accent : "black"
                radius: 5
                opacity: 0.7
            }

            TextField {
                id: input
                Material.accent: wsw.isShowingTeamChatPopup ? "white" : defaultMaterialAccent

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 8
                anchors.leftMargin: 12
                anchors.rightMargin: 12

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
                anchors.topMargin: 0
                anchors.leftMargin: 12
                anchors.rightMargin: 12

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
                        input.anchors.bottomMargin - anchors.topMargin - anchors.bottomMargin - 12
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
    }

    Keys.onPressed: {
        if (wsw.isShowingChatPopup || wsw.isShowingTeamChatPopup) {
            if (event.key == Qt.Key_Escape || event.key == Qt.Key_Back) {
                wsw.closeChatPopup()
            }
        }
    }
}