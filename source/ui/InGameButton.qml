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

    MouseArea {
        id: mouseArea
        hoverEnabled: true
        anchors.fill: parent
        onClicked: root.clicked()
    }

    // Acts as a shadow caster.
    // Putting content inside it is discouraged as antialiasing does not seem to be working in this case
    Item {
        anchors.centerIn: parent
        height: parent.height
        width: mouseArea.containsMouse ? parent.width + 8 : parent.width

        layer.enabled: root.enabled
        layer.effect: ElevationEffect { elevation: 16 }

        transform: Matrix4x4 { matrix: fullHeightTransformMatrix }

        Behavior on width { SmoothedAnimation { duration: 333 } }
    }

    Rectangle {
        anchors.centerIn: parent
        height: parent.height
        width: mouseArea.containsMouse ? parent.width + 16 : parent.width
        radius: 3

        color: mouseArea.containsMouse ? Material.accentColor : Qt.lighter(Material.backgroundColor, 1.25)

        transform: Matrix4x4 { matrix: fullHeightTransformMatrix }

        Behavior on width { SmoothedAnimation { duration: 333 } }
    }

    Label {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pointSize: 14
        font.weight: Font.ExtraBold
        font.letterSpacing: mouseArea.containsMouse ? 1.75 : 1.25
        font.capitalization: Font.AllUppercase

        Behavior on font.letterSpacing { SmoothedAnimation { duration: 333 } }

        transform: Matrix4x4 {
            matrix: wsw.makeSkewXMatrix(label.height, 16.0)
        }
    }
}