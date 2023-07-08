import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

AutoFittingComboBox {
    id: root

    Material.theme: Material.Dark
    Material.accent: "orange"

    property string cvarName: ""
    property bool applyImmediately: true

    property var oldValue

    property bool skipIndexChangeSignal: true

    property var knownHeadingsAndValues

    property var headings
    property var values

    readonly property string placeholder: "- -"

    onKnownHeadingsAndValuesChanged: {
        const value = UI.ui.getCVarValue(cvarName)
        // Make deep copies
        headings = [...knownHeadingsAndValues[0]]
        values = [...knownHeadingsAndValues[1]]
        if (mapValueToIndex(value) < 0) {
            values.push(value)
            headings.push(placeholder)
        }
        model = headings
        // Force a value re-check/index update next frame (for some unknown reasons it has to be postponed)
        oldValue = undefined
    }

    function checkCVarChanges() {
        let value = UI.ui.getCVarValue(cvarName)
        if (value != oldValue) {
            if (applyImmediately || !UI.ui.hasControlPendingCVarChanges(root)) {
                setNewValue(value)
            }
        }
    }

    function setNewValue(value) {
        let index = mapValueToIndex(value)
        if (index < 0) {
            // Add a placeholder if needed. Otherwise just update the placeholder value
            if (headings[headings.length - 1] !== placeholder) {
                headings.push(placeholder)
                values.push(value)
                model = headings
            } else {
                values[values.length - 1] = value
            }
            index = values.length - 1
        } else {
            // Remove the placeholder if it's no longer useful
            if (index != headings.length - 1 && headings[headings.length - 1] === placeholder) {
                values.pop()
                headings.pop()
                model = headings
            }
        }

        oldValue = value
        if (root.currentIndex != index) {
            root.currentIndex = index
        }
    }

    function mapValueToIndex(value) {
        const knownValues = knownHeadingsAndValues[1]
        for (let i = 0; i < knownValues.length; ++i) {
            if (knownValues[i] == value) {
                return i
            }
        }
        return -1
    }

    function rollbackChanges() {
        skipIndexChangeSignal = true
        setNewValue(UI.ui.getCVarValue(cvarName))
        skipIndexChangeSignal = false
    }

    onCurrentIndexChanged: {
        if (skipIndexChangeSignal) {
            return
        }

        let value = values[currentIndex]
        if (applyImmediately) {
            UI.ui.setCVarValue(cvarName, value)
        } else {
            UI.ui.markPendingCVarChanges(root, cvarName, value)
        }
    }

    Component.onCompleted: {
        // Values are already set in onKnownHeadingsAndValuesChanged handler
        UI.ui.registerCVarAwareControl(root)
        skipIndexChangeSignal = false
    }

    Component.onDestruction: UI.ui.unregisterCVarAwareControl(root)
}