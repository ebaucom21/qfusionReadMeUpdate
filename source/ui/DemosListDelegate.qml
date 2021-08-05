import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

MouseArea {
    id: root
    height: compact ? 56 : 22
    hoverEnabled: true

    property bool compact
    property bool selected

    property string demoName
    property string serverName
    property string mapName
    property string gametype
    property string timestamp
    // TODO: An eager retrieval of tags is bad, check whether using a lazy binding (?) to a model property is possible
    property string tags

    property real demoColumnWidth
    property real serverColumnWidth
    property real mapColumnWidth
    property real gametypeColumnWidth
    property real timestampColumnWidth

    property real rowSpacing
    property real labelMargins: rowSpacing / 3

    readonly property color textColor: containsMouse || selected ? Material.accent : Material.foreground
    readonly property real letterSpacing: 0.50

    transitions: Transition {
        AnchorAnimation { duration: 67 }
    }

    Behavior on height {
        NumberAnimation { duration: 67 }
    }

    states: [
        State {
            name: "linear"
            when: !compact
            AnchorChanges {
                target: demoNameLabel
                anchors.left: root.left
                anchors.right: undefined
                anchors.top: undefined
                anchors.bottom: root.bottom
            }
            AnchorChanges {
                target: serverNameLabel
                anchors.left: demoNameLabel.right
                anchors.right: undefined
                anchors.top: undefined
                anchors.bottom: root.bottom
            }
            AnchorChanges {
                target: mapNameLabel
                anchors.left: serverNameLabel.right
                anchors.right: undefined
                anchors.top: undefined
                anchors.bottom: root.bottom
            }
            AnchorChanges {
                target: gametypeLabel
                anchors.left: mapNameLabel.right
                anchors.right: undefined
                anchors.top: undefined
                anchors.bottom: root.bottom
            }
            AnchorChanges {
                target: timestampLabel
                anchors.left: gametypeLabel.right
                anchors.right: undefined
                anchors.top: undefined
                anchors.bottom: root.bottom
            }
            PropertyChanges {
                target: demoNameLabel
                width: demoColumnWidth
            }
            PropertyChanges {
                target: serverNameLabel
                horizontalAlignment: Qt.AlignHCenter
                font.capitalization: Font.AllUppercase
            }
            PropertyChanges {
                target: mapNameLabel
                font.capitalization: Font.AllUppercase
                width: mapColumnWidth
            }
            PropertyChanges {
                target: gametypeLabel
                font.capitalization: Font.AllUppercase
                width: gametypeColumnWidth
            }
            PropertyChanges {
                target: tagsLabel
                visible: false
            }
        },
        State {
            name: "compact"
            when: compact
            AnchorChanges {
                target: demoNameLabel
                anchors.left: root.left
                anchors.right: undefined
                anchors.top: root.top
                anchors.bottom: undefined
            }
            AnchorChanges {
                target: serverNameLabel
                anchors.left: root.left
                anchors.right: undefined
                anchors.top: demoNameLabel.bottom
                anchors.bottom: undefined
            }
            AnchorChanges {
                target: mapNameLabel
                anchors.left: undefined
                anchors.right: gametypeLabel.left
                anchors.top: timestampLabel.bottom
                anchors.bottom: undefined
            }
            AnchorChanges {
                target: gametypeLabel
                anchors.left: undefined
                anchors.right: root.right
                anchors.top: timestampLabel.bottom
                anchors.bottom: undefined
            }
            AnchorChanges {
                target: timestampLabel
                anchors.left: undefined
                anchors.right: root.right
                anchors.top: root.top
                anchors.bottom: undefined
            }
            PropertyChanges {
                target: demoNameLabel
                width: root.width - timestampLabel.implicitWidth - 24
            }
            PropertyChanges {
                target: serverNameLabel
                horizontalAlignment: Qt.AlignLeft
                font.capitalization: Font.MixedCase
            }
            PropertyChanges {
                target: mapNameLabel
                font.capitalization: Font.MixedCase
                width: mapNameLabel.implicitWidth
            }
            PropertyChanges {
                target: gametypeLabel
                font.capitalization: Font.MixedCase
                width: gametypeLabel.implicitWidth
            }
            PropertyChanges {
                target: tagsLabel
                visible: true
            }
        }
    ]

    state: "linear"

    Label {
        id: demoNameLabel
        anchors.margins: labelMargins
        horizontalAlignment: Qt.AlignLeft
        font.capitalization: Font.AllUppercase
        font.weight: Font.Medium
        font.pointSize: 11
        font.letterSpacing: letterSpacing
        elide: Text.ElideRight
        text: demoName
        color: textColor
    }
    Label {
        id: serverNameLabel
        width: serverColumnWidth
        anchors.margins: labelMargins
        horizontalAlignment: Qt.AlignHCenter
        font.capitalization: Font.AllUppercase
        font.weight: Font.Medium
        font.pointSize: 11
        font.letterSpacing: letterSpacing
        elide: Text.ElideMiddle
        text: serverName
        color: textColor
    }
    Label {
        id: mapNameLabel
        anchors.margins: labelMargins
        horizontalAlignment: Qt.AlignHCenter
        font.weight: Font.Medium
        font.pointSize: 11
        font.letterSpacing: letterSpacing
        text: mapName
        color: textColor
    }
    Label {
        id: gametypeLabel
        anchors.margins: labelMargins
        horizontalAlignment: Qt.AlignHCenter
        font.weight: Font.Medium
        font.pointSize: 11
        font.letterSpacing: letterSpacing
        text: gametype
        color: textColor
    }
    Label {
        id: timestampLabel
        width: timestampColumnWidth
        anchors.margins: labelMargins
        horizontalAlignment: Qt.AlignRight
        font.weight: Font.Medium
        font.pointSize: 11
        font.letterSpacing: letterSpacing
        text: timestamp
        color: textColor
    }
    Label {
        id: tagsLabel
        font.weight: Font.Medium
        font.pointSize: 11
        font.letterSpacing: letterSpacing
        anchors.left: parent.left
        anchors.top: serverNameLabel.bottom
        anchors.margins: labelMargins
        width: parent.width
        maximumLineCount: 1
        elide: Text.ElideMiddle
        text: tags
        color: textColor
    }
}