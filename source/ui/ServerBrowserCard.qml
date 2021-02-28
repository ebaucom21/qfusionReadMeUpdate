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

    Rectangle {
        id: header
        anchors.top: parent.top
        width: root.width
        height: 72
        color: {
            let base = Qt.darker(Material.background, 1.5)
            Qt.rgba(base.r, base.g, base.b, 0.3)
        }

        Label {
            id: addressLabel
            anchors {
                top: parent.top
                topMargin: 8
                right: parent.right
                rightMargin: 8
            }
            text: address
            font.pointSize: 12
            font.weight: Font.Medium
        }

        Label {
            id: serverNameLabel
            width: header.width - addressLabel.implicitWidth - 24
            anchors {
                top: parent.top
                topMargin: 8
                left: parent.left
                leftMargin: 8
            }
            text: serverName
            textFormat: Text.StyledText
            font.pointSize: 12
            font.weight: Font.Medium
            wrapMode: Text.Wrap
            maximumLineCount: 1
            elide: Text.ElideRight
        }

        Row {
            anchors {
                bottom: parent.bottom
                bottomMargin: 8
                left: parent.left
                leftMargin: 8
            }
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

        Item {
            id: matchTimeView
            width: root.width
            height: timeLabel.height + timeFlagsLabel.height
            anchors { top: body.top; topMargin: 4 }

            Label {
                id: timeLabel
                visible: height > 0
                height: (typeof(timeFlags) !== "undefined" && !timeFlags) ? implicitHeight : 0
                text: (timeMinutes ? (timeMinutes < 10 ? "0" + timeMinutes : timeMinutes) : "00") +
                      ":" +
                      (timeSeconds ? (timeSeconds < 10 ? "0" + timeSeconds : timeSeconds) : "00")
                font.pointSize: 15
                font.letterSpacing: 8
                font.weight: Font.ExtraBold
                anchors.centerIn: parent
                horizontalAlignment: Qt.AlignHCenter
            }

            Label {
                id: timeFlagsLabel
                visible: height > 0
                height: text.length > 0 ? implicitHeight : 0
                text: formatTimeFlags()
                anchors.centerIn: parent
                font.pointSize: 13
                font.letterSpacing: 8
                font.weight: Font.ExtraBold
                horizontalAlignment: Qt.AlignHCenter

                Connections {
                    target: root
                    onTimeFlagsChanged: {
                        timeFlagsLabel.text = timeFlagsLabel.formatTimeFlags()
                    }
                }

                function formatTimeFlags() {
                    if (!timeFlags) {
                        return ""
                    }

                    if (timeFlags & ServerListModel.Warmup) {
                        if (!!playersTeamList || !!alphaTeamList || !!betaTeamList) {
                            return "WARMUP"
                        }
                        return ""
                    }

                    if (timeFlags & ServerListModel.Countdown) {
                        return "COUNTDOWN"
                    }

                    if (timeFlags & ServerListModel.Finished) {
                        return "FINISHED"
                    }

                    let s = ""
                    if (timeFlags & ServerListModel.SuddenDeath) {
                        s += "SUDDEN DEATH, "
                    } else if (timeFlags & ServerListModel.Overtime) {
                        s += "OVERTIME, "
                    }

                    // Actually never set for warmups. let's not complicate
                    if (timeFlags & ServerListModel.Timeout) {
                        s += "TIMEOUT, "
                    }

                    if (s.length > 2) {
                        s = s.substring(0, s.length - 2)
                    }
                    return s
                }
            }
        }

        Item {
            id: teamScoreView
            visible: height > 0
            width: root.width
            anchors.top: matchTimeView.bottom
            height: (!!alphaTeamList || !!betaTeamList) ? 36 : 0

            Label {
                visible: !!alphaTeamList
                anchors {
                    left: parent.left
                    right: alphaScoreLabel.left
                    leftMargin: 24
                    rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }
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
                anchors {
                    right: parent.horizontalCenter
                    rightMargin: 32
                    verticalCenter: parent.verticalCenter
                }
                text: typeof(alphaTeamList) !== "undefined" &&
                        typeof(alphaTeamScore) !== "undefined" ?
                            alphaTeamScore : "-"
                font.weight: Font.ExtraBold
                font.pointSize: 24
            }

            Label {
                id: betaScoreLabel
                width: implicitWidth
                anchors {
                    left: parent.horizontalCenter
                    leftMargin: 32 - 8 // WTF?
                    verticalCenter: parent.verticalCenter
                }

                text: typeof(betaTeamList) !== "undefined" &&
                        typeof(betaTeamScore) !== "undefined" ?
                            betaTeamScore : "-"
                font.weight: Font.ExtraBold
                font.pointSize: 24
            }

            Label {
                visible: !!betaTeamList
                anchors {
                    left: betaScoreLabel.left
                    right: parent.right
                    leftMargin: 12 + 20 // WTF?
                    rightMargin: 24 + 8 // WTF?
                    verticalCenter: parent.verticalCenter
                }
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

        ServerBrowserPlayersList {
            id: alphaView
            model: alphaTeamList
            showEmptyListHeader: !!alphaTeamList || !!betaTeamList
            width: root.width / 2 - 12
            height: contentHeight
            anchors { top: teamScoreView.bottom; left: parent.left }
        }

        ServerBrowserPlayersList {
            id: betaView
            model: betaTeamList
            showEmptyListHeader: !!alphaTeamList || !!betaTeamList
            width: root.width / 2 - 12
            height: contentHeight
            anchors { top: teamScoreView.bottom; right: parent.right }
        }

        ServerBrowserPlayersList {
            id: playersView
            model: playersTeamList
            anchors { top: teamScoreView.bottom; left: parent.left; right: parent.right }
            height: contentHeight
        }

        Item {
            id: spectatorsView
            visible: height > 0
            anchors {
                left: parent.left
                right: parent.right
                bottom: body.bottom
                bottomMargin: 12
            }

            height: !spectatorsList ? 0 :
                    (spectatorsLabel.implicitHeight + spectatorsLabel.anchors.topMargin +
                    spectatorsFlow.implicitHeight + spectatorsFlow.anchors.topMargin)

            Label {
                anchors {
                    top: parent.top
                    topMargin: 12
                    horizontalCenter: parent.horizontalCenter
                }
                id: spectatorsLabel
                text: "Spectators"
                font.pointSize: 11
                font.weight: Font.Medium
            }

            Flow {
                id: spectatorsFlow
                spacing: 12

                anchors {
                    top: spectatorsLabel.bottom
                    topMargin: 12
                    left: parent.left
                    leftMargin: 8
                    right: parent.right
                    rightMargin: 8
                }

                Repeater {
                    id: spectatorsRepeater
                    model: spectatorsList
                    delegate: Row {
                        spacing: 8
                        Label {
                            text: modelData["name"]
                            font.pointSize: 11
                        }
                        Label {
                            text: wsw.formatPing(modelData["ping"])
                            font.pointSize: 11
                        }
                    }
                }
            }
        }
    }
}