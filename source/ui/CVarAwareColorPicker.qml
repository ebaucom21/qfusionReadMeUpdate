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
        onClicked: popup.openForColor(root.customColor)

        readonly property color crossColor: containsMouse ? Material.accentColor : Material.foreground

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
        focus: true
        dim: false

        x: root.width - 24
        y: -12
        width: 128 + 64 + 16
        height: 128
        transformOrigin: Item.Top
        topMargin: 12
        bottomMargin: 12
        verticalPadding: 8

        property var selectedColor
        property bool hasChanges: false

        function openForColor(customColor) {
            rSlider.value = customColor ? customColor.r : 1.0
            gSlider.value = customColor ? customColor.g : 1.0
            bSlider.value = customColor ? customColor.b : 1.0
            popup.selectedColor = customColor
            popup.hasChanges    = false
            popup.open()
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

        background: Rectangle {
            radius: 5
            color: Material.background
            opacity: 0.67

            layer.enabled: true
            layer.effect: ElevationEffect { elevation: 16 }
        }

        onVisibleChanged: repositionBlurRegion()
        onXChanged: repositionBlurRegion()
        onYChanged: repositionBlurRegion()
        onWidthChanged: repositionBlurRegion()
        onHeightChanged: repositionBlurRegion()

        onAboutToHide: {
            rootItem.leavePopupMode()
        }

        function repositionBlurRegion() {
            if (popup.visible) {
                const globalPos = parent.mapToGlobal(0, 0)
                const inset     = 2

                // TODO: It should not be named "enter/leave" as we violate balancing semantics of calls
                rootItem.enterPopupMode(Qt.rect(globalPos.x + inset + popup.x, globalPos.y + inset + popup.y,
                    background.width - 2 * inset, background.height - 2 * inset))
            }
        }

        contentItem: ColumnLayout {
            Material.theme: Material.Dark
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.leftMargin: 2
                    Layout.preferredWidth: 24
                    color: rSlider.Material.accent
                    text: Math.round(rSlider.value * 255)
                    font.weight: Font.Medium
                    style: Text.Raised
                }
                Slider {
                    id: rSlider
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Material.accent: Qt.rgba(1.0, 0.0, 0.0, 1.0)
                    Material.foreground: Material.accent
                    onValueChanged: popup.updateSelectedColor(rSlider.value, gSlider.value, bSlider.value)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.leftMargin: 2
                    Layout.preferredWidth: 24
                    color: gSlider.Material.accent
                    text: Math.round(gSlider.value * 255)
                    font.weight: Font.Medium
                    style: Text.Raised
                }
                Slider {
                    id: gSlider
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Material.accent: Qt.rgba(0.0, 1.0, 0.0, 1.0)
                    onValueChanged: popup.updateSelectedColor(rSlider.value, gSlider.value, bSlider.value)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.leftMargin: 2
                    Layout.preferredWidth: 24
                    color: bSlider.Material.accent
                    text: Math.round(bSlider.value * 255)
                    font.weight: Font.Medium
                    style: Text.Raised
                }
                Slider {
                    id: bSlider
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Material.accent: Qt.rgba(0.0, 0.0, 1.0, 1.0)
                    onValueChanged: popup.updateSelectedColor(rSlider.value, gSlider.value, bSlider.value)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: -8
                Layout.bottomMargin: -8

                RoundButton {
                    Layout.leftMargin: -4
                    flat: true
                    text: "\u2714"
                    font.family: wsw.symbolsFontFamily
                    enabled: !!popup.selectedColor && popup.hasChanges
                    onClicked: {
                        root.setSelectedCustomColor(popup.selectedColor)
                        popup.close()
                    }
                }

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.preferredHeight: 12
                    radius: 3
                    color: popup.selectedColor ? popup.selectedColor : "transparent"
                    border.width: popup.selectedColor ? 0 : 1
                    border.color: Qt.rgba(0.5, 0.5, 0.5, 0.25)
                }

                RoundButton {
                    Layout.rightMargin: -4
                    flat: true
                    text: "\u2716"
                    font.family: wsw.symbolsFontFamily
                    onClicked: popup.close()
                }
            }
        }
    }
}