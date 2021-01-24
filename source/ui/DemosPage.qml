import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    property var pendingQuery

    readonly property real mapColumnWidth: 100
    readonly property real gametypeColumnWidth: 100
    readonly property real timestampColumnWidth: 144

    readonly property real demoNameWeight: 1.0
    readonly property real serverNameWeight: 1.0

    readonly property real leftListMargin: 100
    readonly property real rightListMargin: 100

    readonly property real rowSpacing: 16

    Component.onCompleted: demosResolver.reload()

    TextField {
        id: queryField
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        horizontalAlignment: Qt.AlignHCenter
        maximumLength: 29
        width: 300
        Material.theme: Material.Dark
        Material.foreground: "white"
        Material.accent: "orange"

        onTextChanged: submitQuery(text)
        onEditingFinished: submitQuery(text)
    }

    Label {
        anchors.left: queryField.right
        anchors.verticalCenter: queryField.verticalCenter
        text: "\uD83D\uDD0D"
        visible: !queryField.text.length
    }

    Label {
        anchors.centerIn: parent
        text: "Nothing found"
        font.pointSize: 11
        visible: demosResolver.isReady && !listView.count
    }

    function submitQuery(query) {
        if (demosResolver.isReady) {
            demosResolver.query(query)
        } else {
            pendingQuery = query
        }
    }

    Connections {
        target: demosResolver
        onIsReadyChanged: {
            if (demosResolver.isReady && pendingQuery) {
                demosResolver.query(pendingQuery)
                pendingQuery = undefined
            }
        }
    }

    RowLayout {
        id: listHeader
        anchors.top: queryField.bottom
        anchors.topMargin: 48
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: leftListMargin
        anchors.rightMargin: rightListMargin
        width: listView.width
        spacing: rowSpacing


        Label {
            Layout.fillWidth: true
            Layout.preferredWidth: demoNameWeight
            horizontalAlignment: Qt.AlignLeft
            font.pointSize: 11
            font.weight: Font.Medium
            text: "Name"
        }
        Label {
            Layout.fillWidth: true
            Layout.preferredWidth: serverNameWeight
            horizontalAlignment: Qt.AlignHCenter
            font.pointSize: 11
            font.weight: Font.Medium
            text: "Server"
        }
        Label {
            Layout.preferredWidth: mapColumnWidth
            horizontalAlignment: Qt.AlignHCenter
            font.pointSize: 11
            font.weight: Font.Medium
            text: "Map"
        }
        Label {
            Layout.preferredWidth: gametypeColumnWidth
            horizontalAlignment: Qt.AlignHCenter
            font.pointSize: 11
            font.weight: Font.Medium
            text: "Gametype"
        }
        Label {
            Layout.preferredWidth: timestampColumnWidth
            horizontalAlignment: Qt.AlignHCenter
            font.pointSize: 11
            font.weight: Font.Medium
            text: "Timestamp"
        }
    }

    ListView {
        id: listView
        model: demosModel
        anchors.top: listHeader.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32
        anchors.left: parent.left
        anchors.leftMargin: leftListMargin
        anchors.right: parent.right
        anchors.rightMargin: rightListMargin
        boundsBehavior: Flickable.StopAtBounds
        spacing: rowSpacing
        clip: true

        delegate: RowLayout {
            width: listView.width
            spacing: 16

            Label {
                Layout.fillWidth: true
                Layout.preferredWidth: demoNameWeight
                font.capitalization: Font.AllUppercase
                font.pointSize: 11
                elide: Text.ElideRight
                text: demoName
            }
            Label {
                Layout.fillWidth: true
                Layout.preferredWidth: serverNameWeight
                horizontalAlignment: Qt.AlignHCenter
                font.capitalization: Font.AllUppercase
                font.pointSize: 11
                elide: Text.ElideMiddle
                text: serverName
            }
            Label {
                Layout.preferredWidth: mapColumnWidth
                horizontalAlignment: Qt.AlignHCenter
                font.capitalization: Font.AllUppercase
                font.pointSize: 11
                text: mapName
            }
            Label {
                Layout.preferredWidth: gametypeColumnWidth
                horizontalAlignment: Qt.AlignHCenter
                font.capitalization: Font.AllUppercase
                font.pointSize: 11
                text: gametype
            }
            Label {
                Layout.preferredWidth: timestampColumnWidth
                horizontalAlignment: Qt.AlignCenter
                font.capitalization: Font.AllUppercase
                font.pointSize: 11
                text: timestamp
            }
        }
    }
}