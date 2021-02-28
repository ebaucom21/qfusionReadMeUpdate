import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

ListView {
    id: root
    interactive: false

    property bool showEmptyListHeader: false

    header: Item {
        width: root.width
        height: visible ? 32 : 0
        visible: typeof(model) !== "undefined" || showEmptyListHeader

        Label {
            text: "Name"
            anchors {
                left: parent.left
                leftMargin: 8
                verticalCenter: parent.verticalCenter
            }
            font.pointSize: 11
            font.weight: Font.Medium
        }

        Row {
            spacing: 8
            anchors {
                right: parent.right
                rightMargin: 8
                verticalCenter: parent.verticalCenter
            }

            Label {
                text: "Score"
                width: 48
                font.pointSize: 11
                font.weight: Font.Medium
                horizontalAlignment: Qt.AlignHCenter
            }

            Label {
                text: "Ping"
                width: 48
                font.pointSize: 11
                font.weight: Font.Medium
                horizontalAlignment: Qt.AlignHCenter
            }
        }
    }

    delegate: Item {
        width: root.width
        height: 24

        Label {
            text: modelData["name"]
            textFormat: Text.StyledText
            font.pointSize: 11
            width: root.width - scoreAndPingRow.width - 8
            maximumLineCount: 1
            wrapMode: Text.Wrap
            elide: Text.ElideRight

            anchors {
                left: parent.left
                leftMargin: 8
                verticalCenter: parent.verticalCenter
            }
        }

        Row {
            id: scoreAndPingRow
            spacing: 8

            anchors {
                right: parent.right
                rightMargin: 8
                verticalCenter: parent.verticalCenter
            }

            Label {
                text: modelData["score"]
                width: 48
                font.pointSize: 11
                horizontalAlignment: Qt.AlignHCenter
                // Race scores could break layout:
                // TODO: Provide more space for this in a single-column layout
                maximumLineCount: 1
                wrapMode: Text.Wrap
                elide: Text.ElideRight
            }

            Label {
                text: wsw.formatPing(modelData["ping"])
                width: 48
                font.pointSize: 11
                horizontalAlignment: Qt.AlignHCenter
            }
        }
    }
}