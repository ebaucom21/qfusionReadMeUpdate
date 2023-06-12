import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root

    implicitWidth: slider.implicitWidth + 16
    implicitHeight: slider.implicitHeight

    property string cvarName: ""
    property bool applyImmediately: true
    property bool suppressSignals: true

    property int fractionalPartDigits: 2
    property real textFieldWidth: 56

    property bool putBoxToTheLeft: true

    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize

    property real value

    states: [
        State {
            name: "boxToTheLeft"
            when: putBoxToTheLeft
            PropertyChanges {
                target: slider
                anchors.left: undefined
                anchors.right: root.right
            }
            PropertyChanges {
                target: textField
                anchors.left: root.left
                anchors.right: undefined
            }
        },
        State {
            name: "boxToTheRight"
            when: !putBoxToTheLeft
            PropertyChanges {
                target: slider
                anchors.left: root.left
                anchors.right: undefined
            }
            PropertyChanges {
                target: textField
                anchors.left: undefined
                anchors.right: root.right
            }
        }
    ]

    onValueChanged: {
        if (value !== slider.value) {
            const suppressSliderSignals = slider.suppressSignals
            slider.suppressSignals      = true
            slider.value                = value
            slider.suppressSignals      = suppressSliderSignals
        }

        const text = valueToText(value)
        if (text != textField.text) {
            const suppressFieldSignals = textField.suppressSignals
            textField.suppressSignals  = true
            textField.text             = text
            textField.suppressSignals  = suppressFieldSignals
        }

        if (!root.suppressSignals) {
            if (applyImmediately) {
                wsw.setCVarValue(cvarName, value)
            } else {
                wsw.markPendingCVarChanges(root, cvarName, value)
            }
        }
    }

    function checkCVarChanges() {
        let newValue = getCurrNumericVarValue()
        if (value != newValue) {
            if (applyImmediately || !wsw.hasControlPendingCVarChanges(root)) {
                value = newValue
            }
        }
    }

    function rollbackChanges() {
        root.suppressSignals      = true
        slider.suppressSignals    = true
        textField.suppressSignals = true

        value = getCurrNumericVarValue()

        textField.suppressSignals = false
        slider.suppressSignals    = false
        root.suppressSignals      = false
    }

    function valueToText(v) {
        return (v + 0.0).toFixed(fractionalPartDigits)
    }

    function textToValue(text) {
        let v = parseFloat(text)
        if (isNaN(v)) {
            v = 0.0
        }
        return Math.max(slider.from, Math.min(v, slider.to))
    }

    function getCurrNumericVarValue() {
        return textToValue(wsw.getCVarValue(cvarName))
    }

    Component.onCompleted: {
        value                     = getCurrNumericVarValue()
        slider.value              = value
        textField.text            = valueToText(value)
        wsw.registerCVarAwareControl(root)
        suppressSignals           = false
        slider.suppressSignals    = false
        textField.suppressSignals = false
    }

    Component.onDestruction: wsw.unregisterCVarAwareControl(root)

    Slider {
        id: slider
        width: root.implicitWidth - textField.width - 8

        anchors.verticalCenter: parent.verticalCenter

        Material.theme: Material.Dark
        Material.accent: "orange"

        property bool suppressSignals: true

        onValueChanged: {
            if (!slider.suppressSignals) {
                if (root.value !== slider.value) {
                    root.value = slider.value
                }
            }
        }
    }

    TextField {
        id: textField

        anchors.verticalCenter: slider.verticalCenter
        anchors.verticalCenterOffset: +2
        width: textFieldWidth
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignHCenter
        maximumLength: 3 + fractionalPartDigits
        selectByMouse: false

        property bool suppressSignals: true

        background: Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -4
            width: parent.width
            height: 34
            radius: 1
            color: "transparent"
            border.color: parent.activeFocus ? Material.accent : Material.foreground
            border.width: 1
            opacity: parent.activeFocus ? 1.0 : 0.1
        }

        onEditingFinished: {
            if (!textField.suppressSignals) {
                const numericValue = textToValue(textField.text)
                const newTextValue = valueToText(numericValue)
                if (newTextValue !== textField.text) {
                    textField.suppressSignals = true
                    textField.text            = newTextValue
                    textField.suppressSignals = false
                }
                if (root.value !== numericValue) {
                    root.value = numericValue
                }
            }
        }
    }
}