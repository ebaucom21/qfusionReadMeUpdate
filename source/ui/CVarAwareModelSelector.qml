import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Rectangle {
    id: root
    color: Qt.rgba(1.0, 1.0, 1.0, 0.025)

    property color modelColor
    property int selectedIndex
    property string selectedValue
    property bool applyImmediately: true
    property bool suppressSignals: true
    property string cvarName
    property string defaultModel
    property int defaultModelIndex: -1
    readonly property var models: wsw.playerModels

    ToolButton {
        width: 56; height: 56
        anchors.horizontalCenter: parent.left
        anchors.verticalCenter: parent.verticalCenter
        icon.source: "qrc:/ModelLeftArrow.svg"
        onClicked: selectedIndex = selectedIndex ? selectedIndex - 1 : models.length - 1
    }

    // TODO: Draw the model
    Label {
        anchors.centerIn: parent
        text: models[selectedIndex]
    }

    ToolButton {
        width: 56; height: 56
        anchors.horizontalCenter: parent.right
        anchors.verticalCenter: parent.verticalCenter
        icon.source: "qrc:/ModelRightArrow.svg"
        onClicked: selectedIndex = selectedIndex + 1 < models.length ? selectedIndex + 1 : 0
    }

    Label {
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        font.weight: Font.Bold
        font.letterSpacing: 1.75
        font.capitalization: Font.Capitalize
        style: Text.Raised
        text: selectedValue
    }

    onSelectedIndexChanged: {
        selectedValue = models[selectedIndex]
        if (!suppressSignals) {
            if (applyImmediately) {
                wsw.setCVarValue(cvarName, selectedValue)
            } else {
                wsw.markPendingCVarChanges(root, cvarName, selectedValue)
            }
        }
    }

    function mapValueToIndex(value) {
        for (let i = 0; i < models.length; ++i) {
            if (models[i] == value) {
                return i
            }
        }
        return defaultModelIndex
    }

    function getActualIndex() {
        return mapValueToIndex(wsw.getCVarValue(cvarName).toLowerCase())
    }

    function checkCVarChanges() {
        const index = getActualIndex()
        if (index !== selectedIndex) {
            suppressSignals = true
            selectedIndex = index
            suppressSignals = false
        }
    }

    function rollbackChanges() {
        selectedIndex = getActualIndex()
    }

    Component.onCompleted: {
        defaultModelIndex = mapValueToIndex(defaultModel)
        console.assert(defaultModelIndex >= 0, "Failed to find an index for a default model")
        selectedIndex = getActualIndex()
        selectedValue = models[selectedIndex]
        wsw.registerCVarAwareControl(root)
        suppressSignals = false
    }

    Component.onDestruction: wsw.unregisterCVarAwareControl(root)
}