import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

Item {
    id: teamScoreView
    visible: height > 0
    width: root.width
    anchors.top: matchTimeView.bottom
    implicitHeight: (!!alphaTeamList || !!betaTeamList) ? 36 : 0

    property var alphaTeamList
    property var betaTeamList
    property var alphaTeamScore
    property var betaTeamScore

    Label {
        visible: !!alphaTeamList
        anchors.left: parent.left
        anchors.right: alphaScoreLabel.left
        anchors.leftMargin: 24
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Qt.AlignLeft
        textFormat: Text.StyledText
        text: alphaTeamName || ""
        maximumLineCount: 1
        elide: Text.ElideRight
        font.letterSpacing: 4
        font.weight: Font.Medium
        font.pointSize: 16
    }

    Label {
        id: alphaScoreLabel
        width: implicitWidth
        anchors.right: parent.horizontalCenter
        anchors.rightMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        font.weight: Font.ExtraBold
        font.pointSize: 24
        text: typeof(alphaTeamList) !== "undefined" && typeof(alphaTeamScore) !== "undefined" ? alphaTeamScore : "-"
    }

    Label {
        id: betaScoreLabel
        width: implicitWidth
        anchors.left: parent.horizontalCenter
        anchors.leftMargin: 32 - 8 // WTF?
        anchors.verticalCenter: parent.verticalCenter
        font.weight: Font.ExtraBold
        font.pointSize: 24
        text: typeof(betaTeamList) !== "undefined" && typeof(betaTeamScore) !== "undefined" ? betaTeamScore : "-"
    }

    Label {
        visible: !!betaTeamList
        anchors.left: betaScoreLabel.left
        anchors.right: parent.right
        anchors.leftMargin: 12 + 20 // WTF?
        anchors.rightMargin: 24 + 8 // WTF?
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Qt.AlignRight
        textFormat: Text.StyledText
        text: betaTeamName || ""
        maximumLineCount: 1
        elide: Text.ElideLeft
        font.letterSpacing: 4
        font.weight: Font.Medium
        font.pointSize: 16
    }
}