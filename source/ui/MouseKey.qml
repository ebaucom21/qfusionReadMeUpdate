import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

KeyboardKey {
    Connections {
        target: keysAndBindings
        onMouseKeyBindingChanged: {
            if (changedQuakeKey === quakeKey) {
                group = keysAndBindings.getMouseKeyBindingGroup(quakeKey)
            }
        }
    }
    Component.onCompleted: {
        group = keysAndBindings.getMouseKeyBindingGroup(quakeKey)
    }
}