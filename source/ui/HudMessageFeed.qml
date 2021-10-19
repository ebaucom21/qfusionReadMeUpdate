import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root
    width: implicitWidth
    height: implicitHeight
    implicitWidth: 720
    implicitHeight: list.height + 20
    opacity: 0.9

    Connections {
        target: hudDataModel
        onIsMessageFeedFadingOutChanged: {
            if (isMessageFeedFadingOut) {
                fadeOutAnim.start()
            } else {
                fadeOutAnim.stop()
                opacity = 0.9
            }
        }
    }

    PropertyAnimation {
        id: fadeOutAnim
        target: root
        property: "opacity"
        to: 0.0
        duration: 2500
    }

    ListView {
        id: list
        anchors.top: parent.top
        anchors.topMargin: 10
        anchors.horizontalCenter: parent.horizontalCenter
        model: hudDataModel.getMessageFeedModel()
        width: parent.width - 20
        height: contentHeight
        spacing: 3
        delegate: Label {
            width: list.width
            horizontalAlignment: Qt.AlignLeft
            elide: Text.ElideRight
            maximumLineCount: 1
            font.pointSize: 12
            font.letterSpacing: 1
            font.weight: Font.DemiBold
            textFormat: Text.StyledText
            style: Text.Raised
            text: message

            opacity: 0.0
            Component.onCompleted: opacity = 1.0
            Behavior on opacity { NumberAnimation { duration: 100 } }
        }
    }
}