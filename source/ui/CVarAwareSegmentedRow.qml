import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12

RowLayout {
    id: root
    property var modelValues
    property var modelTitles
    property real desiredSegmentWidth: -1

    property var selectedIndex: -1
    property var selectedValue: undefined

    property string cvarName
    property bool applyImmediately: true
    property bool suppressSignals: true

    onSelectedIndexChanged: {
        if (!suppressSignals) {
            if (selectedIndex >= 0) {
                selectedValue = modelValues[selectedIndex]
            } else {
                selectedValue = undefined
            }
        }
    }

    onSelectedValueChanged: {
        if (!suppressSignals) {
            if (applyImmediately) {
                UI.ui.setCVarValue(cvarName, selectedValue)
            } else {
                UI.ui.markPendingCVarChanges(root, cvarName, value)
            }
        }
    }

    onModelValuesChanged: {
        const saved = suppressSignals
        suppressSignals = true
        selectedIndex = -1
        setupRepeaterModel()
        suppressSignals = saved
    }
    onModelTitlesChanged: {
        const saved = suppressSignals
        suppressSignals = true
        selectedIndex = -1
        setupRepeaterModel()
        suppressSignals = saved
    }

    Component.onCompleted: {
        setupRepeaterModel()
        selectedIndex = mapValueToIndex(UI.ui.getCVarValue(cvarName))
        if (selectedIndex >= 0) {
            selectedValue = modelValues[selectedIndex]
        } else {
            selectedValue = undefined
        }
        UI.ui.registerCVarAwareControl(root)
        suppressSignals = false
    }

    Component.onDestruction: UI.ui.unregisterCVarAwareControl(root)

    function setupRepeaterModel() {
        const realModelTitles = []
        for (let i = 0; i < modelTitles.length; ++i) {
            realModelTitles.push(modelTitles[i])
            if (i + 1 !== modelTitles.length) {
                realModelTitles.push(undefined)
            }
        }
        repeater.model = realModelTitles
    }

    function checkCVarChanges() {
        const actualValue = UI.ui.getCVarValue(cvarName)
        const index = mapValueToIndex(actualValue)
        // TODO: Is it redundant?
        if (selectedIndex !== index) {
            selectedIndex = index
        }
    }

    function rollbackChanges() {
        suppressSignals = true
        selectedIndex = mapValueToIndex(UI.ui.getCVarValue(cvarName))
        suppressSignals = false
    }

    function mapValueToIndex(value) {
        for (let i = 0; i < modelValues.length; ++i) {
            if (modelValues[i] == value) {
                return i
            }
        }
        return -1
    }

    Repeater {
        id: repeater
        delegate: Label {
            Layout.preferredWidth: desiredSegmentWidth
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            text: (index % 2) ? "/" : modelTitles[index / 2]
            font.weight: (index % 2) ? Font.Medium : Font.Black
            font.letterSpacing: (mouseArea.containsMouse && index !== 2 * root.selectedIndex) ? 2.0 : 1.5
            font.pointSize: 11
            font.underline: (index === 2 * root.selectedIndex)
            Behavior on font.letterSpacing { SmoothedAnimation { duration: 200 } }
            color: (mouseArea.containsMouse || index === 2 * root.selectedIndex) ? Material.accent : Material.foreground
            MouseArea {
                id: mouseArea
                anchors.fill: parent
                enabled: !(index % 2)
                hoverEnabled: true
                onClicked: selectedIndex = index / 2
            }
        }
    }
}