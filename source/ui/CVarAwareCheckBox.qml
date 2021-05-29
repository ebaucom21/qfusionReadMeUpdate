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
        const value = fromNative(wsw.getCVarValue(cvarName))
        if (checked != value) {
            if (applyImmediately || !wsw.hasControlPendingCVarChanges(root)) {
                checked = value
            }
        }
    }

    function rollbackChanges() { checked = fromNative(wsw.getCVarValue(cvarName)) }

    function fromNative(value) { return value != 0; }
    function toNative(value) { return value ? "1" : "0"; }

    onClicked: {
        const convertedValue = toNative(checked)
        if (applyImmediately) {
            wsw.setCVarValue(cvarName, convertedValue)
        } else {
            wsw.markPendingCVarChanges(root, cvarName, convertedValue)
        }
    }

    Component.onCompleted: {
        checked = fromNative(wsw.getCVarValue(cvarName))
        wsw.registerCVarAwareControl(root)
    }

    Component.onDestruction: wsw.unregisterCVarAwareControl(root)
}