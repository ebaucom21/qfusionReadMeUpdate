import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

ColorPickerItem {
    id: root
    implicitWidth: 14
    height: 14
    clip: false

    property color color
    property bool selected

    haloColor: color

    contentItem: Rectangle {
        anchors.fill: parent
        color: root.color
        radius: 2

        Rectangle {
            anchors.bottom: parent.top
            anchors.bottomMargin: 4
            width: 14
            height: 2
            color: root.color
            visible: root.selected
        }
    }

    NumberAnimation {
        id: bulgeAnim
        target: contentItem
        property: "radius"
        duration: 150
    }

    function startExtraHoverAnims() {
        bulgeAnim.stop()
        bulgeAnim.from = contentItem.radius
        bulgeAnim.to = contentItem.height / 2
        bulgeAnim.start()
    }

    function stopExtraHoverAnims() {
        bulgeAnim.stop()
        bulgeAnim.from = contentItem.radius
        bulgeAnim.to = 2
        bulgeAnim.start()
    }
}
