import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    implicitWidth: 256
    implicitHeight: 72
    width: implicitWidth
    height: implicitHeight

    Label {
        anchors.right: separator.left
        anchors.rightMargin: 8
        anchors.baseline: separator.baseline
        font.weight: Font.ExtraBold
        font.pointSize: 40
        font.letterSpacing: 3
        style: Text.Raised
        textFormat: Text.PlainText
        text: hudDataModel.matchTimeMinutes
    }

    Label {
        id: separator
        anchors.centerIn: parent
        font.weight: Font.ExtraBold
        font.pointSize: 48
        style: Text.Raised
        text: ":"
    }

    Label {
        anchors.left: separator.right
        anchors.leftMargin: 8
        anchors.baseline: separator.baseline
        font.weight: Font.ExtraBold
        font.pointSize: 40
        font.letterSpacing: 3
        style: Text.Raised
        textFormat: Text.PlainText
        text: hudDataModel.matchTimeSeconds
    }
}