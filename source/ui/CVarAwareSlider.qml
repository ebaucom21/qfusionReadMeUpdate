import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Slider {
    id: root

    Material.theme: Material.Dark
    Material.accent: "orange"

    property string cvarName: ""
    property bool applyImmediately: true
    property bool suppressSignals: true

    function checkCVarChanges() {
        let newValue = UI.ui.getCVarValue(cvarName)
        if (value != newValue) {
            if (applyImmediately || !UI.ui.hasControlPendingCVarChanges(root)) {
                value = newValue
            }
        }
    }

    function rollbackChanges() {
        suppressSignals = true
        value = UI.ui.getCVarValue(cvarName)
        suppressSignals = false
    }

    onValueChanged: {
        if (!suppressSignals) {
            if (applyImmediately) {
                UI.ui.setCVarValue(cvarName, value)
            } else {
                UI.ui.markPendingCVarChanges(root, cvarName, value)
            }
        }
    }

    Component.onCompleted: {
        value = UI.ui.getCVarValue(cvarName)
        UI.ui.registerCVarAwareControl(root)
        suppressSignals = false
    }

    Component.onDestruction: UI.ui.unregisterCVarAwareControl(root)
}