import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import net.warsow 2.6

Item {
    id: root
    implicitHeight: parent.height
    clip: false

    property string text: ""
    property int quakeKey: -1
    property int rowSpan: 1
    property int group: 0
    property real rowSpacing
    property bool hidden: false

    property bool externallyHighlighted
    property bool highlighted: mouseArea.containsMouse || externallyHighlighted

    readonly property color highlightColor: keysAndBindings.colorForGroup(root.group)
    readonly property color highlightBackground: Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.075)

    Rectangle {
        color: !hidden ? (root.group ? highlightBackground : Qt.rgba(0, 0, 0, 0.3)) : "transparent"
        radius: 5
        width: parent.width
        height: parent.height * rowSpan + rowSpacing * (rowSpan - 1)

        border {
            width: highlighted ? 2 : 0
            color: root.group ? highlightColor : Material.accentColor
        }
        anchors {
            top: parent.top
            left: parent.left
        }

        Component.onCompleted: keysAndBindings.registerKeyItem(root, quakeKey)
        Component.onDestruction: keysAndBindings.unregisterKeyItem(root, quakeKey)

        Rectangle {
            anchors {
                left: parent.left
                leftMargin: 5
                verticalCenter: parent.verticalCenter
            }
            width: 5
            height: 5
            radius: 2.5
            visible: !!root.group && root.visible && root.enabled
            color: highlightColor
        }

        MouseArea {
            id: mouseArea
            enabled: !root.hidden && root.enabled
            hoverEnabled: true
            anchors.centerIn: parent
            width: parent.width - 10
            height: parent.height - 10
            onContainsMouseChanged: {
                keysAndBindings.onKeyItemContainsMouseChanged(root, quakeKey, mouseArea.containsMouse)
            }
        }

        Label {
            anchors.centerIn: parent
            text: !root.hidden ? root.text : ""
            font.pointSize: 11
            font.weight: Font.Medium
            color: Material.foreground
        }
    }
}