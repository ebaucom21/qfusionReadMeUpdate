import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtGraphicalEffects 1.12

MouseArea {
    id: root

    hoverEnabled: true
    implicitWidth: 108
    implicitHeight: 40

    property string text
    property string iconPath
    property bool checked

    readonly property bool highlighted: checked || containsMouse

    Rectangle {
        id: shadowCaster
        anchors.centerIn: parent
        width: root.containsMouse ? root.width + 12 : root.width
        height: root.containsMouse ? root.height + 4 : root.height
        Behavior on width { SmoothedAnimation { duration: 333 } }
        Behavior on height { SmoothedAnimation { duration: 333 } }
        color: "transparent"
        layer.enabled: true
        layer.effect: ElevationEffect { elevation: 12 }
        transform: Matrix4x4 { matrix: wsw.makeSkewXMatrix(height, 15.0) }
    }

    // We have to separate these items due to antialiasing/layer conflicts
    Rectangle {
        anchors.centerIn: parent
        width: shadowCaster.width
        height: shadowCaster.height
        radius: 3
        color: root.highlighted ? Material.accent : Qt.lighter(Material.background, 1.25)
        Behavior on color { ColorAnimation { duration: 96 } }
        transform: Matrix4x4 { matrix: wsw.makeSkewXMatrix(height, 15.0) }
    }

    Image {
        id: icon
        visible: !root.highlighted
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.margins: 12
        width: 20
        height: 20
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
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 12
        horizontalAlignment: Qt.AlignHCenter
        maximumLineCount: 1
        elide: Text.ElideMiddle
        text: root.text
        font.weight: Font.Medium
        font.letterSpacing: 1
        font.capitalization: Font.AllUppercase
    }
}