import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root
    property string title
    property var model
    property color baseColor: "black"

    property int playersPerRow: 3
    readonly property int numPlayers: model ? model.length : 0

    visible: numPlayers
    implicitHeight: visible ?
        (label.anchors.topMargin + label.implicitHeight + label.anchors.topMargin +
        column.anchors.topMargin + column.implicitHeight + column.anchors.bottomMargin + 4) : 0


    Rectangle {
        anchors.fill: parent
        color: baseColor
        opacity: 0.5
    }

    Label {
        id: label
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.75
        font.pointSize: 12
        font.weight: Font.Bold
        text: root.title
        style: Text.Raised
    }

    ColumnLayout {
        id: column
        width: parent.width - 8
        anchors.top: label.bottom
        anchors.topMargin: 16 + 2
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 16

        Repeater {
            id: rowsRepeater
            model: numPlayers ? Math.max(numPlayers / playersPerRow, 1) : 0
            delegate: Row {
                Layout.alignment: Qt.AlignHCenter
                readonly property int rowIndex: index
                spacing: 16
                Repeater {
                    model: (rowIndex !== Math.floor(numPlayers / playersPerRow)) ?
                        playersPerRow : (numPlayers % playersPerRow)
                    delegate: Row {
                        id: playerItem
                        spacing: 8
                        readonly property int listIndex: rowIndex * playersPerRow + index
                        Label {
                            width: implicitWidth + 8
                            horizontalAlignment: Qt.AlignLeft
                            font.weight: Font.Bold
                            font.pointSize: 12
                            font.letterSpacing: 1
                            style: Text.Raised
                            text: root.model[listIndex]["name"]
                        }
                        Label {
                            width: implicitWidth + 12
                            horizontalAlignment: Qt.AlignLeft
                            font.weight: Font.Bold
                            font.pointSize: 12
                            font.letterSpacing: 1
                            style: Text.Raised
                            text: root.model[listIndex]["ping"]
                        }
                    }
                }
            }
        }
    }
}