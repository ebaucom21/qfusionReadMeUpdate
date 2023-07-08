import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

WswCheckBox {
    id: root

    Material.foreground: "white"
    Material.accent: "orange"

    property string cvarName: ""
    property bool applyImmediately: true
    property var valueConverters: undefined

    function checkCVarChanges() {
        const value = fromNative(UI.ui.getCVarValue(cvarName))
        if (checked != value) {
            if (applyImmediately || !UI.ui.hasControlPendingCVarChanges(root)) {
                checked = value
            }
        }
    }

    function rollbackChanges() { checked = fromNative(UI.ui.getCVarValue(cvarName)) }

    function fromNative(value) { return value != 0; }
    function toNative(value) { return value ? "1" : "0"; }

    onClicked: {
        const convertedValue = toNative(checked)
        if (applyImmediately) {
            UI.ui.setCVarValue(cvarName, convertedValue)
        } else {
            UI.ui.markPendingCVarChanges(root, cvarName, convertedValue)
        }
    }

    Component.onCompleted: {
        checked = fromNative(UI.ui.getCVarValue(cvarName))
        UI.ui.registerCVarAwareControl(root)
    }

    Component.onDestruction: UI.ui.unregisterCVarAwareControl(root)
}