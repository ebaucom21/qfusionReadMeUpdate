import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    layer.enabled: true
    layer.effect: ElevationEffect { elevation: 64 }

    Rectangle {
        width: parent.width
        height: parent.height
        anchors.bottom: parent.bottom
        color: Material.background
        opacity: 0.7
        radius: 2
    }

    RowLayout {
        id: nameLabel
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: progressBar.width

        Label {
            font.pointSize: 11
            font.letterSpacing: 1
            font.weight: Font.Bold
            font.capitalization: Font.AllUppercase
            style: Text.Raised
            text: demoPlayer.mapName + " - " + demoPlayer.gametype
        }

        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            horizontalAlignment: Qt.AlignHCenter
            font.pointSize: 11
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
            font.weight: Font.Bold
            style: Text.Raised
            text: demoPlayer.demoName
        }

        Label {
            font.pointSize: 11
            font.letterSpacing: 1
            font.weight: Font.Bold
            style: Text.Raised
            text: demoPlayer.timestamp
        }
    }

    ProgressBar {
        id: progressBar
        anchors.top: nameLabel.bottom
        anchors.topMargin: 8
        width: parent.width - 32
        anchors.horizontalCenter: parent.horizontalCenter

        indeterminate: demoPlayer.isPaused

        from: 0.0
        to: demoPlayer.duration
        value: demoPlayer.progress
    }

    MouseArea {
        anchors.horizontalCenter: progressBar.horizontalCenter
        anchors.verticalCenter: progressBar.verticalCenter
        width: progressBar.width
        height: 16
        onClicked: demoPlayer.seek(mouse.x / width)
    }

    Label {
        anchors.left: progressBar.left
        anchors.top: progressBar.bottom
        anchors.topMargin: 8
        text: demoPlayer.formatDuration(demoPlayer.progress)
        font.weight: Font.Bold
        font.letterSpacing: 1
        font.pointSize: 11
        style: Text.Raised
    }

    Label {
        anchors.right: progressBar.right
        anchors.top: progressBar.bottom
        anchors.topMargin: 8
        text: demoPlayer.formatDuration(demoPlayer.duration)
        font.weight: Font.Bold
        font.letterSpacing: 1
        font.pointSize: 11
        style: Text.Raised
    }

    RowLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: progressBar.bottom

        Button {
            flat: true
            display: AbstractButton.TextOnly
            Layout.preferredWidth: 64
            text: "\u23F8"
            font.pointSize: 20
            onClicked: demoPlayer.pause()
        }

        Button {
            flat: true
            display: AbstractButton.TextOnly
            Layout.preferredWidth: 64
            text: "\u23F9"
            font.pointSize: 20
            onClicked: demoPlayer.stop()
        }
    }
}