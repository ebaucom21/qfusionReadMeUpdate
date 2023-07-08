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

    readonly property var fullHeightTransformMatrix: UI.ui.makeSkewXMatrix(height, 20.0)

    MouseArea {
        id: mouseArea
        hoverEnabled: true
        anchors.fill: parent
        onClicked: root.clicked()
    }

    // Reduces opacity of the dropped shadow
    Item {
        anchors.fill: parent
        opacity: 0.67
        z: -1

        // Acts as a shadow caster.
        // Putting content inside it is discouraged as antialiasing does not seem to be working in this case
        Item {
            anchors.centerIn: parent
            height: parent.height
            width: body.width

            layer.enabled: root.enabled
            layer.effect: ElevationEffect { elevation: 16 }

            transform: Matrix4x4 { matrix: fullHeightTransformMatrix }
        }
    }

    Rectangle {
        id: body
        anchors.centerIn: parent
        height: parent.height
        width: mouseArea.containsMouse ? parent.width + 16 : parent.width
        radius: 3

        color: mouseArea.containsMouse ? Material.accentColor : Qt.lighter(Material.backgroundColor, 1.35)

        transform: Matrix4x4 { matrix: fullHeightTransformMatrix }

        Behavior on width { SmoothedAnimation { duration: 333 } }
    }

    Rectangle {
        id: iconPlaceholder
        anchors.left: body.left
        anchors.verticalCenter: body.verticalCenter
        anchors.leftMargin: 20
        width: 12
        height: 12
        radius: 1
        opacity: mouseArea.containsMouse ? 1.0 : 0.7
        transform: Matrix4x4 { matrix: UI.ui.makeSkewXMatrix(height, 19.0) }
    }

    Label {
        id: label
        anchors.left: iconPlaceholder.right
        anchors.verticalCenter: iconPlaceholder.verticalCenter
        anchors.leftMargin: 16
        text: root.text
        font.pointSize: 13.5
        font.weight: Font.ExtraBold
        font.letterSpacing: mouseArea.containsMouse ? 1.75 : 1.25
        font.capitalization: Font.AllUppercase

        Behavior on font.letterSpacing { SmoothedAnimation { duration: 333 } }

        transform: Matrix4x4 { matrix: UI.ui.makeSkewXMatrix(height, 16.0) }
    }
}