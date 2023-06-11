import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

RowLayout {
    id: root
    spacing: 8
    clip: false

    property string cvarName
    property bool applyImmediately: true

    property var customColor
    property int selectedIndex: -1
    property color selectedColor: typeof(customColor) !== "undefined" ? customColor : wsw.consoleColors[selectedIndex]

    Repeater {
        model: wsw.consoleColors

        delegate: ColorPickerColorItem {
            layoutIndex: index
            selected: layoutIndex === root.selectedIndex
            color: wsw.consoleColors[index]
            onMouseEnter: root.expandAt(index)
            onMouseLeave: root.shrinkBack()
            onClicked: root.selectedIndex = index
        }
    }

    ColorPickerColorItem {
        visible: !!customColor
        layoutIndex: wsw.consoleColors.length
        selected: layoutIndex === root.selectedIndex
        color: customColor ? customColor : "transparent"
        onMouseEnter: root.expandAt(index)
        onMouseLeave: root.shrinkBack()
        onClicked: root.selectedIndex = wsw.consoleColors.length
    }

    ColorPickerItem {
        id: cross
        layoutIndex: wsw.consoleColors.length + 1
        haloColor: Material.accentColor
        onMouseEnter: root.expandAt(index)
        onMouseLeave: root.shrinkBack()
        onClicked: popup.openSelf(root.customColor)

        readonly property color crossColor:
            containsMouse ? Material.accentColor : Material.foreground

        contentItem: Item {
            anchors.fill: parent
            Rectangle {
                color: cross.crossColor
                anchors.centerIn: parent
                width: parent.width - 2
                height: 4
            }
            Rectangle {
                color: cross.crossColor
                anchors.centerIn: parent
                width: 4
                height: parent.height - 2
            }
        }
    }

    function checkCVarChanges() {
        let [index, maybeNewCustomColor] = getCVarData()
        if (index !== selectedIndex || maybeNewCustomColor !== customColor) {
            if (applyImmediately || !wsw.hasControlPendingCVarChanges(root)) {
                customColor = maybeNewCustomColor
                selectedIndex = index
            }
        }
    }

    function rollbackChanges() {
        let [index, maybeCustomColor] = getCVarData()
        customColor = maybeCustomColor
        selectedIndex = index
    }

    function indexOfColor(maybeColor) {
        let colors = wsw.consoleColors
        // Compare by hex strings
        let givenString = '' + maybeColor
        for (let i = 0; i < colors.length; ++i) {
            if (('' + colors[i]) === givenString) {
                return i
            }
        }
        return -1
    }

    function getCVarData() {
        let rawString = wsw.getCVarValue(cvarName)
        let maybeColor = wsw.colorFromRgbString(rawString)
        if (!maybeColor) {
            return [-1, undefined]
        }
        let index = indexOfColor(maybeColor)
        if (index != -1) {
            return [index, undefined]
        }
        return [wsw.consoleColors.length, maybeColor]
    }

    function expandAt(hoveredIndex) {
        for (let i = 0; i < children.length; ++i) {
            let child = children[i]
            if (!(child instanceof ColorPickerItem)) {
                continue
            }
            let childIndex = child.layoutIndex
            if (childIndex == hoveredIndex) {
                continue
            }
            child.startShift(childIndex < hoveredIndex)
        }
    }

    function shrinkBack() {
        for (let i = 0; i < children.length; ++i) {
            let child = children[i]
            if (child instanceof ColorPickerItem) {
                child.revertShift()
            }
        }
    }

    function toQuakeColorString(color) {
        // Let Qt convert it to "#RRGGBB". This is more robust than a manual floating-point -> 0.255 conversion.
        let qtColorString = '' + color
        let r = parseInt(qtColorString.substring(1, 3), 16)
        let g = parseInt(qtColorString.substring(3, 5), 16)
        let b = parseInt(qtColorString.substring(5, 7), 16)
        return r + " " + g + " " + b
    }

    function updateCVarColor(color) {
        let value = toQuakeColorString(color)
        if (applyImmediately) {
            wsw.setCVarValue(cvarName, value)
        } else {
            wsw.markPendingCVarChanges(root, cvarName, value)
        }
    }

    function setSelectedCustomColor(color) {
        customColor = color
        let customColorIndex = wsw.consoleColors.length
        if (selectedIndex != customColorIndex) {
            // This triggers updateCVarColor()
            selectedIndex = customColorIndex
        } else {
            // The index remains the same, force color update
            updateCVarColor(color)
        }
    }

    onSelectedIndexChanged: {
        if (selectedIndex >= 0) {
            updateCVarColor(wsw.consoleColors[selectedIndex] || customColor)
        }
    }

    Component.onCompleted: {
        wsw.registerCVarAwareControl(root)
        let [index, maybeCustomColor] = getCVarData()
        customColor = maybeCustomColor
        selectedIndex = index
    }

    Component.onDestruction: wsw.unregisterCVarAwareControl(root)

    Popup {
        id: popup
        modal: true
        focus: true
        dim: false
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: wsw.desiredPopupWidth
        height: wsw.desiredPopupHeight

        // TODO: Figure out a better way to pass args to the contentComopnent instance
        property real initialSliderRValue: 1.0
        property real initialSliderGValue: 1.0
        property real initialSliderBValue: 1.0

        property var selectedColor
        property bool hasChanges: false

        function openSelf(customColor) {
            popup.initialSliderRValue = customColor ? customColor.r : 1.0
            popup.initialSliderGValue = customColor ? customColor.g : 1.0
            popup.initialSliderBValue = customColor ? customColor.b : 1.0
            popup.selectedColor = customColor
            popup.hasChanges = false
            popup.parent = rootItem.windowContentItem
            rootItem.enterPopupMode()
            wsw.registerNativelyDrawnItemsOccluder(background)
            popup.open()
        }

        function closeSelf() {
            if (opened) {
                popup.close()
                popup.selectedColor = undefined
                popup.hasChanges = false
                rootItem.leavePopupMode()
                wsw.unregisterNativelyDrawnItemsOccluder(background)
            }
        }

        function updateSelectedColor(r, g, b) {
            if (opened) {
                // Don't modify the selected color partially
                const newColor = Qt.rgba(r, g, b, 1.0)
                if (selectedColor != newColor) {
                    selectedColor = newColor
                    hasChanges = true
                }
            }
        }

        background: PopupBackground {}

        contentItem: PopupContentItem {
            title: "Select a color"
            active: popup.visible
            hasAcceptButton: popup.hasChanges
            acceptButtonText: "Select"
            onAccepted: {
                 root.setSelectedCustomColor(popup.selectedColor)
                 popup.closeSelf()
            }
            onRejected: popup.closeSelf()
            onDismissed: popup.closeSelf()

            contentComponent: RowLayout {
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: 32
                    Layout.rightMargin: 32
                    width: 32; height: 32; radius: 3
                    color: popup.selectedColor ? popup.selectedColor : "transparent"
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    spacing: -12

                    Slider {
                        id: rSlider
                        value: popup.initialSliderRValue
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        Material.accent: Qt.rgba(1.0, 0.0, 0.0, 1.0)
                        onValueChanged: popup.updateSelectedColor(rSlider.value, gSlider.value, bSlider.value)
                    }
                    Slider {
                        id: gSlider
                        value: popup.initialSliderGValue
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        Material.accent: Qt.rgba(0.0, 1.0, 0.0, 1.0)
                        onValueChanged: popup.updateSelectedColor(rSlider.value, gSlider.value, bSlider.value)
                    }
                    Slider {
                        id: bSlider
                        value: popup.initialSliderBValue
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        Material.accent: Qt.rgba(0.0, 0.0, 1.0, 1.0)
                        onValueChanged: popup.updateSelectedColor(rSlider.value, gSlider.value, bSlider.value)
                    }
                }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: 32
                    Layout.rightMargin: 32
                    width: 32; height: 32; radius: 3
                    color: popup.selectedColor ? popup.selectedColor : "transparent"
                }
            }
        }
    }
}