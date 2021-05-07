import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    Component.onCompleted: {
        scanTimer.start()
        applyFilter()
    }

    Component.onDestruction: wsw.stopServerListUpdates()

    function applyFilter() {
        let flags = 0
        if (fullCheckBox.checked) {
            flags |= Wsw.ShowFullServers
        }
        if (emptyCheckBox.checked) {
            flags |= Wsw.ShowEmptyServers
        }
        wsw.startServerListUpdates(flags)
    }

    RowLayout {
        id: optionsBar
        anchors.top: parent.top
        anchors.topMargin: 16
        anchors.left: parent.left
        anchors.right: parent.right

        Item { Layout.fillWidth: true }

        CheckBox {
            id: fullCheckBox
            Material.theme: checked ? Material.Light : Material.Dark
            text: "Show full"
            onCheckedChanged: applyFilter()
            // TODO: Lift the reusable component
            Component.onCompleted: {
                contentItem.font.pointSize = 12
                contentItem.color = Material.foreground
            }
        }
        CheckBox {
            id: emptyCheckBox
            Material.theme: checked ? Material.Light : Material.Dark
            text: "Show empty"
            onCheckedChanged: applyFilter()
            Component.onCompleted: {
                contentItem.font.pointSize = 12
                contentItem.color = Material.foreground
            }
        }
    }

    TableView {
        id: tableView
        visible: !scanTimer.running
        anchors.top: optionsBar.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom
        // Ok, let it go slightly out of bounds in side directions
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width + columnSpacing
        columnSpacing: 28
        rowSpacing: 40
        interactive: true
        flickableDirection: Flickable.VerticalFlick
        clip: true

        onRowsChanged: {
            if (!rows) {
                scanTimer.start()
            }
        }

        model: serverListModel

        delegate: ServerBrowserCard {
            implicitWidth: root.width / 2
            visible: typeof(model["serverName"]) !== "undefined"
            serverName: model["serverName"] || ""
            mapName: model["mapName"] || ""
            gametype: model["gametype"] || ""
            address: model["address"] || ""
            numPlayers: model["numPlayers"] || 0
            maxPlayers: model["maxPlayers"] || 0
            timeMinutes: model["timeMinutes"]
            timeSeconds: model["timeSeconds"]
            timeFlags: model["timeFlags"]
            alphaTeamName: model["alphaTeamName"]
            betaTeamName: model["betaTeamName"]
            alphaTeamScore: model["alphaTeamScore"]
            betaTeamScore: model["betaTeamScore"]
            alphaTeamList: model["alphaTeamList"]
            betaTeamList: model["betaTeamList"]
            playersTeamList: model["playersTeamList"]
            spectatorsList: model["spectatorsList"]

            onImplicitHeightChanged: forceLayoutTimer.start()
        }

        Timer {
            id: forceLayoutTimer
            interval: 1
            onTriggered: tableView.forceLayout()
        }

        Connections {
            target: serverListModel
            onWasReset: tableView.forceLayout()
        }
    }

    Timer {
        id: scanTimer
        interval: 1750
    }

    Loader {
        anchors.centerIn: parent
        active: scanTimer.running
        sourceComponent: ColumnLayout {
            Component.onCompleted: opacity = 1.0
            opacity: 0.0
            Behavior on opacity { NumberAnimation { duration: 500 } }
            spacing: 24
            width: progressBar.implicitWidth
            ProgressBar {
                id: progressBar
                indeterminate: true
            }
            Label {
                Layout.fillWidth: true
                font.pointSize: 12
                horizontalAlignment: Qt.AlignHCenter
                text: "Discovering servers\u2026"
            }
        }
    }

    Loader {
        anchors.centerIn: parent
        active: !scanTimer.running && !tableView.rows
        sourceComponent: ColumnLayout {
            Component.onCompleted: opacity = 1.0
            opacity: 0.0
            Behavior on opacity { NumberAnimation { duration: 500 } }
            spacing: 12
            width: 320
            Label {
                Layout.fillWidth: true
                font.pointSize: 12
                horizontalAlignment: Qt.AlignHCenter
                text: "No servers found"
            }
            Button {
                Layout.preferredWidth: 144
                Layout.alignment: Qt.AlignHCenter
                highlighted: true
                text: "Play offline"
                // TODO: This should be less hacky
                onClicked: centralOverlay.activePageTag = centralOverlay.pageLocalGame
            }
        }
    }
}