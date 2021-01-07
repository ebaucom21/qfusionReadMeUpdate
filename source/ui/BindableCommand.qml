import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

MouseArea {
    id: root

    property string text
    property string command
    property int commandNum
    property color highlightColor

    property real defaultLabelWidth
    property real defaultLabelHeight

    property bool isBound
    property bool externallyHighlighted
    readonly property bool highlighted: isBound && (containsMouse || externallyHighlighted)

    hoverEnabled: true
    implicitHeight: Math.max(marker.height, defaultLabelHeight) + 4
    implicitWidth: marker.width + defaultLabelWidth + 12

    Component.onCompleted: keysAndBindings.registerCommandItem(root, commandNum)
    Component.onDestruction: keysAndBindings.unregisterCommandItem(root, commandNum)

    onContainsMouseChanged: keysAndBindings.onCommandItemContainsMouseChanged(root, commandNum, containsMouse)

    Rectangle {
        id: marker
        visible: isBound
        width: 5
        height: 5
        radius: 2.5
        color: highlighted ? highlightColor : Material.foreground
        anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
        }
    }

    Label {
        id: label
        text: root.text
        color: highlighted ? highlightColor : Material.foreground
        font.underline: highlighted
        font.weight: highlighted ? Font.Bold : Font.Normal
        anchors {
            left: marker.right
            leftMargin: 12
            verticalCenter: parent.verticalCenter
        }

        Component.onCompleted: {
            root.defaultLabelWidth = label.implicitWidth
            root.defaultLabelHeight = label.implicitHeight
        }
    }
}
