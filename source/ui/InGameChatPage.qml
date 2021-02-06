import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    Rectangle {
        anchors.fill: parent
        opacity: 0.15
        color: "black"
    }

    property bool isDisplayingTeamChat: false

    RowLayout {
        id: header
        width: parent.width - 16
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter

        Item {
            Layout.fillWidth: true
        }

        Button {
            flat: true
            visible: wsw.hasTeamChat
            Material.theme: Material.Dark
            text: isDisplayingTeamChat ? "switch to common" : "switch to team"
            onClicked: isDisplayingTeamChat = !isDisplayingTeamChat
        }

        // Unused for now
    }

    Connections {
        target: wsw
        onHasTeamChatChanged: {
            isDisplayingTeamChat = false
        }
    }

    RichChatList {
        id: chatList
        model: isDisplayingTeamChat ? richTeamChatModel : richChatModel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: inputFrame.top
        // It look slightly less due to the content alginment
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        anchors.topMargin: 8
        anchors.bottomMargin: 12
        clip: true
        onCountChanged: positionViewAtEnd()
        onModelChanged: positionViewAtEnd()
    }

    Rectangle {
        anchors.horizontalCenter: inputFrame.horizontalCenter
        anchors.verticalCenter: inputFrame.verticalCenter
        color: Material.background
        width: inputFrame.width + 8
        height: inputFrame.height + 4
        radius: 3
    }

    ScrollView {
        id: inputFrame
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        leftPadding: 4
        rightPadding: 4
        height: Math.min(Math.max(72, input.implicitHeight), 128)

        TextArea {
            id: input
            selectByMouse: false
            selectByKeyboard: false
            wrapMode: TextEdit.Wrap
            font.letterSpacing: 1.2
            font.pointSize: 12

            onTextChanged: {
                // TODO: Count bytes/respect native code limitations on the number of bytes
                if (length > 200) {
                    remove(200, length)
                }
            }

            Keys.onPressed: {
                if (event.key === Qt.Key_Enter) {
                    wsw.sendChatMessage(text, isDisplayingTeamChat)
                    // We should clear the key that is being entered now, defer to the next frame
                    clearOnNextFrameTimer.start()
                }
            }

            Timer {
                id: clearOnNextFrameTimer
                interval: 1
                onTriggered: input.clear()
            }
        }
    }
}