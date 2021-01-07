import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Column {
    id: root
    spacing: 12
    property var model
    property color highlightColor

    Repeater {
        model: root.model

        BindableCommand {
            text: modelData["text"]
            command: modelData["command"]
            commandNum: modelData["commandNum"]
            isBound: modelData["isBound"]
            highlightColor: root.highlightColor
        }
    }
}