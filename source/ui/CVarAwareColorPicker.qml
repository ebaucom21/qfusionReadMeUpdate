import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

RowLayout {
    id: root
    spacing: 10
    clip: false

    property string cvarName
    property bool applyImmediately: true

    property var customColor
    property int selectedIndex: -1
    property color selectedColor: customColor ? customColor : wsw.consoleColors[selectedIndex]

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
        width: 280
        height: 240

        property var selectedColor
        property bool hasChanges: false

        function openSelf(customColor) {
            rSlider.value = customColor ? customColor.r : 1.0
            gSlider.value = customColor ? customColor.g : 1.0
            bSlider.value = customColor ? customColor.b : 1.0
            popup.selectedColor = customColor
            popup.hasChanges = false
            popup.parent = rootItem.windowContentItem
            rootItem.enablePopupOverlay()
            popup.open()
        }

        function closeSelf() {
            if (!opened) {
                return
            }

            popup.close()
            popup.selectedColor = undefined
            popup.hasChanges = false
            rootItem.disablePopupOverlay()
        }

        function updateSelectedColor() {
            if (!opened) {
                return
            }
            // Don't modify the selected color partially
            let color = Qt.rgba(0, 0, 0, 1.0)
            color.r = rSlider.value
            color.g = gSlider.value
            color.b = bSlider.value
            selectedColor = color
            hasChanges = true
        }

        background: Rectangle {
            Rectangle {
                width: parent.width
                height: 3
                anchors.top: parent.top
                color: Material.accentColor
            }

            width: parent.width
            height: parent.height
            focus: true
            radius: 3
            color: Material.backgroundColor
            layer.enabled: parent.enabled
            layer.effect: ElevationEffect { elevation: 32 }

            Label {
                id: titleLabel
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 20
                text: "Select a custom color"
                font.weight: Font.Light
                font.pointSize: 15
            }

            RowLayout {
                id: slidersRow
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: titleLabel.bottom
                anchors.topMargin: 24
                anchors.bottom: cancelButton.top
                anchors.bottomMargin: 8
                spacing: -8

                Slider {
                    id: rSlider
                    Layout.fillHeight: true
                    orientation: Qt.Vertical
                    Material.accent: Qt.rgba(1.0, 0.0, 0.0, 1.0)
                    onValueChanged: popup.updateSelectedColor()
                }
                Slider {
                    id: gSlider
                    Layout.fillHeight: true
                    orientation: Qt.Vertical
                    Material.accent: Qt.rgba(0.0, 1.0, 0.0, 1.0)
                    onValueChanged: popup.updateSelectedColor()
                }
                Slider {
                    id: bSlider
                    Layout.fillHeight: true
                    orientation: Qt.Vertical
                    Material.accent: Qt.rgba(0.0, 0.0, 1.0, 1.0)
                    onValueChanged: popup.updateSelectedColor()
                }
            }

            Rectangle {
                anchors {
                    verticalCenter: slidersRow.verticalCenter
                    left: parent.left
                    leftMargin: 32
                }
                width: 21; height: 21; radius: 2
                color: popup.selectedColor ? popup.selectedColor : "transparent"
            }

            Rectangle {
                anchors {
                    verticalCenter: slidersRow.verticalCenter
                    right: parent.right
                    rightMargin: 32
                }
                width: 21; height: 21; radius: 2
                color: popup.selectedColor ? popup.selectedColor : "transparent"
            }

            Button {
                id: cancelButton
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.leftMargin: 20
                flat: true
                text: "Cancel"

                states: [
                    State {
                        when: !popup.hasChanges
                        AnchorChanges {
                            target: cancelButton
                            anchors.left: undefined
                            anchors.right: undefined
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                ]

                transitions: Transition {
                    AnchorAnimation { duration: 25 }
                }

                onClicked: popup.closeSelf()
            }

            Button {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.rightMargin: 20
                flat: true
                highlighted: true
                visible: popup.hasChanges
                text: "Select"
                onClicked: {
                    root.setSelectedCustomColor(popup.selectedColor)
                    popup.closeSelf()
                }
            }

            Keys.onPressed: {
                if (event.key === Qt.Key_Escape) {
                    popup.closeSelf()
                }
            }
        }
    }
}