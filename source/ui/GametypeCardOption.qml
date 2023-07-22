import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

MouseArea {
    id: root

    hoverEnabled: true
    implicitHeight: 72 + 16
    implicitWidth: 72 + 16

    property string text
    property string iconPath
    property bool checked

    readonly property bool highlighted: checked || containsMouse

    Rectangle {
        anchors.centerIn: parent
        width: root.containsMouse ? root.width + 4 : root.width
        height: root.containsMouse ? root.height + 12 : root.height
        Behavior on height { SmoothedAnimation { duration: 333 } }
        Behavior on width { SmoothedAnimation { duration: 333 } }
        radius: 3
        color: root.highlighted ? Material.accent : Qt.lighter(Material.background, 1.35)
        layer.enabled: true
        layer.effect: ElevationEffect { elevation: 12 }
    }

    Image {
        id: icon
        visible: !root.highlighted
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: label.top
        anchors.bottomMargin: 12
        width: 32
        height: 32
        smooth: true
        mipmap: true
        source: root.iconPath
    }

    ColorOverlay {
        visible: root.highlighted
        anchors.fill: icon
        source: icon
        color: "white"
    }

    Label {
        id: label
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        horizontalAlignment: Qt.AlignHCenter
        maximumLineCount: 1
        elide: Text.ElideMiddle
        text: root.text
        font.weight: Font.Medium
        font.letterSpacing: 1
        font.capitalization: Font.AllUppercase
    }
}