import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root
    Layout.fillWidth: true
    height: 40

    property string text

    signal clicked()

    readonly property var fullHeightTransformMatrix: wsw.makeSkewXMatrix(height, 20.0)

    // Acts as a shadow caster.
    // Putting content inside it is discouraged as antialiasing does not seem to be working in this case
    MouseArea {
        id: mouseArea
        hoverEnabled: true
        anchors.fill: parent
        onClicked: root.clicked()

        transform: Matrix4x4 { matrix: fullHeightTransformMatrix }

        layer.enabled: root.enabled
        layer.effect: ElevationEffect { elevation: 16 }
    }

    Rectangle {
        id: rectangle
        width: parent.width
        height: parent.height
        anchors.centerIn: parent
        radius: 3
        color: mouseArea.containsMouse ? Material.accentColor : Qt.lighter(Material.backgroundColor, 1.25)

        transform: Matrix4x4 { matrix: fullHeightTransformMatrix }
    }

    Label {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pointSize: 15
        font.weight: Font.Bold
        font.letterSpacing: 1
        font.capitalization: Font.AllUppercase

        transform: Matrix4x4 {
            matrix: wsw.makeSkewXMatrix(label.height, 16.0)
        }
    }
}