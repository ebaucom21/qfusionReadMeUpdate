import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root

    implicitHeight: mainKeyboardPane.height + arrowPadPane.height + arrowPadPane.anchors.topMargin

    signal bindingRequested(int quakeKey)
    signal unbindingRequested(int quakeKey)
    signal keySelected(int quakeKey)

    property bool isInEditorMode

    KeyboardPane {
        id: mainKeyboardPane
        isInEditorMode: root.isInEditorMode

        anchors {
            top: parent.top
            topMargin: 32
            horizontalCenter: parent.horizontalCenter
        }

        width: parent.width
        rowHeight: 26
        rowModels: [
            keysAndBindings.keyboardMainPadRow1,
            keysAndBindings.keyboardMainPadRow2,
            keysAndBindings.keyboardMainPadRow3,
            keysAndBindings.keyboardMainPadRow4,
            keysAndBindings.keyboardMainPadRow5,
            keysAndBindings.keyboardMainPadRow6
        ]

        onBindingRequested: root.bindingRequested(quakeKey)
        onUnbindingRequested: root.unbindingRequested(quakeKey)
        onKeySelected: root.keySelected(quakeKey)
    }

    MouseKeysPane {
        height: arrowPadPane.height
        isInEditorMode: root.isInEditorMode

        anchors {
            top: mainKeyboardPane.bottom
            topMargin: 24
            left: mainKeyboardPane.left
            right: arrowPadPane.left
            rightMargin: 32
        }

        onBindingRequested: root.bindingRequested(quakeKey)
        onUnbindingRequested: root.unbindingRequested(quakeKey)
        onKeySelected: root.keySelected(quakeKey)
    }

    KeyboardPane {
        id: arrowPadPane
        isInEditorMode: root.isInEditorMode

        anchors {
            top: mainKeyboardPane.bottom
            topMargin: 24
            right: numPadPane.left
            rightMargin: 32
        }

        width: mainKeyboardPane.width / 5
        rowHeight: 26
        rowModels: [
            keysAndBindings.keyboardArrowPadRow1,
            keysAndBindings.keyboardArrowPadRow2,
            keysAndBindings.keyboardArrowPadRow3,
            keysAndBindings.keyboardArrowPadRow4,
            keysAndBindings.keyboardArrowPadRow5
        ]

        onBindingRequested: root.bindingRequested(quakeKey)
        onUnbindingRequested: root.unbindingRequested(quakeKey)
        onKeySelected: root.keySelected(quakeKey)
    }

    KeyboardPane {
        id: numPadPane
        isInEditorMode: root.isInEditorMode

        anchors {
            top: mainKeyboardPane.bottom
            topMargin: 24
            right: mainKeyboardPane.right
        }

        // One extra key
        width: 4 * arrowPadPane.width / 3
        rowHeight: 26
        rowModels: [
            keysAndBindings.keyboardNumPadRow1,
            keysAndBindings.keyboardNumPadRow2,
            keysAndBindings.keyboardNumPadRow3,
            keysAndBindings.keyboardNumPadRow4,
            keysAndBindings.keyboardNumPadRow5,
        ]

        onBindingRequested: root.bindingRequested(quakeKey)
        onUnbindingRequested: root.unbindingRequested(quakeKey)
        onKeySelected: root.keySelected(quakeKey)
    }
}