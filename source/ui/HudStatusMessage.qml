import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    implicitWidth: stackView.currentItem ? stackView.currentItem.implicitWidth + 96 : 0
    implicitHeight: stackView.height

    Connections {
        target: hudDataModel
        onStatusMessageChanged: {
            stackView.clear()
            stackView.push(component, {"message": statusMessage})
            timer.start()
        }
    }

    Timer {
        id: timer
        interval: 2500
        onTriggered: stackView.clear()
    }

    StackView {
        id: stackView
        width: rootItem.width
        height: 96
        anchors.centerIn: parent
    }

    Component {
        id: component
        Label {
            property string message
            text: message
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            font.weight: Font.Medium
            font.pointSize: 20
            font.letterSpacing: 2
            font.wordSpacing: 2
            font.capitalization: Font.SmallCaps
        }
    }
}