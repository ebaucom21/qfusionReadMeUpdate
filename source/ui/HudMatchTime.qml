import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    implicitWidth: 256
    implicitHeight: 54 + 16
    width: implicitWidth
    height: implicitHeight

    readonly property Scale scaleTransform: Scale {
        xScale: 1.00
        yScale: 0.85
    }

    Label {
        anchors.right: separator.left
        anchors.rightMargin: 8
        anchors.baseline: separator.baseline
        transform: scaleTransform
        font.weight: Font.ExtraBold
        font.pointSize: 40
        font.letterSpacing: 3
        style: Text.Raised
        textFormat: Text.PlainText
        text: hudDataModel.matchTimeMinutes
    }

    Label {
        id: separator
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -8
        transform: scaleTransform
        font.weight: Font.ExtraBold
        font.pointSize: 48
        style: Text.Raised
        textFormat: Text.PlainText
        text: ":"
    }

    Label {
        anchors.left: separator.right
        anchors.leftMargin: 8
        anchors.baseline: separator.baseline
        transform: scaleTransform
        font.weight: Font.ExtraBold
        font.pointSize: 40
        font.letterSpacing: 3
        style: Text.Raised
        textFormat: Text.PlainText
        text: hudDataModel.matchTimeSeconds
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        font.weight: Font.ExtraBold
        font.pointSize: 16
        font.letterSpacing: 3
        style: Text.Raised
        textFormat: Text.PlainText
        text: hudDataModel.matchState
    }
}