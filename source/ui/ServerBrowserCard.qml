import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root
    implicitHeight: header.height + body.height

    property string serverName
    property string mapName
    property string gametype
    property string address
    property int numPlayers
    property int maxPlayers

    property var alphaTeamName
    property var betaTeamName
    property var alphaTeamScore
    property var betaTeamScore

    property var timeMinutes
    property var timeSeconds
    property var timeFlags

    property var alphaTeamList
    property var betaTeamList
    property var playersTeamList
    property var spectatorsList

    readonly property bool showEmptyTeamListHeader: !!alphaTeamList || !!betaTeamList

    Rectangle {
        id: header
        anchors.top: parent.top
        width: root.width
        height: 72
        color: {
            const base = Qt.darker(Material.background, 1.5)
            Qt.rgba(base.r, base.g, base.b, 0.3)
        }

        Label {
            id: addressLabel
            anchors.top: parent.top
            anchors.topMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 8
            text: address
            font.pointSize: 12
            font.weight: Font.Medium
        }

        Label {
            id: serverNameLabel
            width: header.width - addressLabel.implicitWidth - 24
            anchors.top: parent.top
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.leftMargin: 8
            text: serverName
            textFormat: Text.StyledText
            font.pointSize: 12
            font.weight: Font.Medium
            wrapMode: Text.Wrap
            maximumLineCount: 1
            elide: Text.ElideRight
        }

        Row {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            anchors.left: parent.left
            anchors.leftMargin: 8
            spacing: 8

            Label {
                text: mapName
                textFormat: Text.StyledText
                font.pointSize: 11
            }
            Label {
                text: "-"
                font.pointSize: 11
            }
            Label {
                text: gametype
                textFormat: Text.StyledText
                font.pointSize: 11
            }
            Label {
                text: "-"
                font.pointSize: 11
            }
            Label {
                text: numPlayers + "/" + maxPlayers
                font.weight: Font.ExtraBold
                font.pointSize: 12
                color: numPlayers !== maxPlayers ? Material.foreground : "red"
            }
            Label {
                text: "players"
                font.pointSize: 11
            }
        }
    }

    Rectangle {
        id: body
        anchors.top: header.bottom
        width: root.width
        color: Qt.rgba(1.0, 1.0, 1.0, 0.05)

        height: exactContentHeight ? exactContentHeight + spectatorsView.anchors.bottomMargin +
                    (matchTimeView.visible ? matchTimeView.anchors.topMargin : 0) : 0

        readonly property real exactContentHeight:
                spectatorsView.height + matchTimeView.height + teamScoreView.height +
                Math.max(alphaView.contentHeight, betaView.contentHeight, playersView.contentHeight)

        ServerBrowserTimeView {
            id: matchTimeView
            width: root.width
            height: implicitHeight
            anchors.top: body.top
            anchors.topMargin: 4
            timeMinutes: root.timeMinutes
            timeSeconds: root.timeSeconds
            timeFlags: root.timeFlags
        }

        ServerBrowserScoreView {
            id: teamScoreView
            visible: implicitHeight > 0
            width: root.width
            anchors.top: matchTimeView.bottom
            height: implicitHeight
            alphaTeamList: root.alphaTeamList
            betaTeamList: root.betaTeamList
            alphaTeamScore: root.alphaTeamScore
            betaTeamScore: root.betaTeamScore
        }

        ServerBrowserPlayersList {
            id: alphaView
            model: alphaTeamList
            showEmptyListHeader: root.showEmptyTeamListHeader
            width: root.width / 2 - 12
            height: contentHeight
            anchors.top: teamScoreView.bottom
            anchors.left: parent.left
        }

        ServerBrowserPlayersList {
            id: betaView
            model: betaTeamList
            showEmptyListHeader: root.showEmptyTeamListHeader
            width: root.width / 2 - 12
            height: contentHeight
            anchors.top: teamScoreView.bottom
            anchors.right: parent.right
        }

        ServerBrowserPlayersList {
            id: playersView
            model: playersTeamList
            anchors.top: teamScoreView.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: contentHeight
        }

        ServerBrowserSpecsList {
            id: spectatorsView
            model: root.spectatorsList
            height: implicitHeight
            visible: implicitHeight > 0
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: body.bottom
            anchors.bottomMargin: 12
        }
    }
}