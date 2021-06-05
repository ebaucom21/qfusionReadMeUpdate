import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root
    implicitHeight: fieldWidth
    implicitWidth: fieldWidth + 0.3 * buttonWidth
    property string cvarName
    property bool applyImmediately: true
    property bool drawNativePart
    property bool displayUnderlay: false
    property real nativePartOpacity: 1.0

    property int value
    property var model
    property real desiredWidthOrHeight: -1
    property color color: "white"

    readonly property real fieldWidth: 64
    readonly property real buttonWidth: 40
    readonly property real buttonShift:
        buttonWidth * (-0.15 + 0.3 * (desiredWidthOrHeight - 16.0) / (64.0 - 16.0))

    Component.onCompleted: wsw.registerCVarAwareControl(root)
    Component.onDestruction: wsw.unregisterCVarAwareControl(root)

    onValueChanged: {
        if (applyImmediately) {
            wsw.setCVarValue(cvarName, value)
        } else {
            wsw.markPendingCVarChanges(root, cvarName, value)
        }
    }

    function checkCVarChanges() {
        const actualValue = wsw.getCVarValue(cvarName)
        if (actualValue != value) {
            if (applyImmediately || !wsw.hasControlPendingCVarChanges(root)) {
                value = actualValue
            }
        }
    }

    function rollbackChanges() {
        value = wsw.getCVarValue(cvarName)
    }

    Rectangle {
        visible: displayUnderlay
        anchors.centerIn: parent
        width: fieldWidth
        height: fieldWidth
        color: Qt.rgba(1, 1, 1, 0.03)
    }

    NativelyDrawnImage {
        visible: drawNativePart && value
        anchors.centerIn: parent
        desiredSize: desiredWidthOrHeight > 0 ? Qt.size(desiredWidthOrHeight, desiredWidthOrHeight) : Qt.size(-1, -1)
        materialName: value ? model[value - 1] : ""
        opacity: nativePartOpacity
        color: root.color
    }

    Label {
        visible: !value
        anchors.centerIn: parent
        font.pointSize: 9
        font.weight: Font.Light
        opacity: nativePartOpacity
        text: "(off)"
    }

    RoundButton {
        anchors.horizontalCenter: parent.left
        anchors.horizontalCenterOffset: -buttonShift
        anchors.verticalCenter: parent.verticalCenter
        Material.theme: pressed ? Material.Light : Material.Dark
        flat: true
        highlighted: down
        width: buttonWidth
        height: buttonWidth
        text: "\u25C2"
        onClicked: value = value ? (value - 1) : model.length
    }

    RoundButton {
        anchors.horizontalCenter: parent.right
        anchors.horizontalCenterOffset: +buttonShift
        Behavior on anchors.horizontalCenterOffset { SmoothedAnimation { duration: 50 } }
        anchors.verticalCenter: parent.verticalCenter
        Material.theme: pressed ? Material.Light : Material.Dark
        flat: true
        highlighted: down
        width: buttonWidth
        height: buttonWidth
        text: "\u25B8"
        onClicked: value = (value + 1) % (model.length + 1)
    }
}