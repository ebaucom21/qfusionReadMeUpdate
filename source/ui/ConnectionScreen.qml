import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
    id: root

    RadialGradient {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.darker(Material.background, 1.1) }
            GradientStop { position: 0.9; color: Qt.darker(Material.background, 1.9) }
        }
    }

    Image {
        id: logo
        width: Math.min(implicitWidth, parent.width - 32)
        fillMode: Image.PreserveAspectFit
        anchors.centerIn: parent
        source: "logo.webp"
    }
}