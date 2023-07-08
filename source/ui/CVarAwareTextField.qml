import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

TextField {
    id: root

    Material.theme: Material.Dark
    Material.accent: "orange"

    property string cvarName: ""
    property bool applyImmediately: true

    function checkCVarChanges() {
        let actualValue = UI.ui.getCVarValue(cvarName)
        if (actualValue != text) {
            if (applyImmediately || !UI.ui.hasControlPendingCVarChanges(root)) {
                text = actualValue
            }
        }
    }

    function rollbackChanges() {
        text = UI.ui.getCVarValue(cvarName)
    }

    onTextEdited: {
        if (applyImmediately) {
            UI.ui.setCVarValue(cvarName, text)
        } else {
            UI.ui.markPendingCVarChanges(root, cvarName, text)
        }
    }

    Component.onCompleted: {
        text = UI.ui.getCVarValue(cvarName)
        UI.ui.registerCVarAwareControl(root)
    }

    Component.onDestruction: UI.ui.unregisterCVarAwareControl(root)
}