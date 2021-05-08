import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root
    readonly property var availableCrosshairs: hudDataModel.getAvailableCrosshairs()
    readonly property var availableStrongCrosshairs: hudDataModel.getAvailableStrongCrosshairs()
    readonly property var crosshairSizeTitles: ["16", "32", "64"]
    readonly property var crosshairSizeValues: [16, 32, 64]
    readonly property var isNotInTransition: root.StackView.view && !root.StackView.view.busy

    GridLayout {
        id: regularSettingsGrid
        width: 0.66 * parent.width
        anchors.top: parent.top
        anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        columns: 2
        columnSpacing: 40
        rowSpacing: 16 + 5 * (Math.min(1080, rootItem.height) - 720) / (1080 - 720)

        SettingsLabel {
            text: "Mouse sensitivity"
        }

        CVarAwareSlider {
            from: 0.1
            to: 10.0
            cvarName: "sensitivity"
        }

        SettingsLabel {
            text: "Crosshair damage color"
        }

        CVarAwareColorPicker {
            cvarName: "cg_crosshair_damage_color"
        }

        SettingsLabel {
            text: "Additional strong crosshair"
        }

        CVarAwareCrosshairSelector {
            drawNativePart: root.isNotInTransition
            nativePartOpacity: popupOverlay.visible ? 0.3 : 1.0
            model: availableStrongCrosshairs
            cvarName: "cg_crosshair_strong"
        }

        SettingsLabel {
            text: "Use separate settings per weapon"
        }

        CVarAwareCheckBox {
            id: separateSettingsCheckBox
            cvarName: "cg_separate_weapon_settings"
        }
    }

    GridLayout {
        visible: !separateSettingsCheckBox.checked
        width: 0.66 * parent.width
        anchors.top: regularSettingsGrid.bottom
        anchors.topMargin: regularSettingsGrid.rowSpacing
        anchors.horizontalCenter: regularSettingsGrid.horizontalCenter
        columns: regularSettingsGrid.columns
        columnSpacing: regularSettingsGrid.columnSpacing
        rowSpacing: regularSettingsGrid.rowSpacing

        SettingsLabel {
            text: "Crosshair"
        }

        CVarAwareCrosshairSelector {
            drawNativePart: root.isNotInTransition
            nativePartOpacity: popupOverlay.visible ? 0.1 : 1.0
            desiredWidthOrHeight: sizeSelector.selectedValue || -1
            color: colorPicker.selectedColor || "white"
            model: availableCrosshairs
            cvarName: "cg_crosshair"
        }

        SettingsLabel {
            text: "Crosshair size"
        }

        CVarAwareSegmentedRow {
            id: sizeSelector
            desiredSegmentWidth: 18
            modelTitles: crosshairSizeTitles
            modelValues: crosshairSizeValues
            cvarName: "cg_crosshair_size"
        }

        SettingsLabel {
            text: "Crosshair color"
        }

        CVarAwareColorPicker {
            id: colorPicker
            cvarName: "cg_crosshair_color"
        }
    }

    Rectangle {
        visible: layout.visible
        anchors.centerIn: layout
        width: layout.width + 48
        height: layout.height + 36
        radius: 2
        color: Qt.rgba(0, 0, 0, 0.07)
    }

    ColumnLayout {
        id: layout
        visible: separateSettingsCheckBox.checked
        anchors.top: regularSettingsGrid.bottom
        anchors.topMargin: 48
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 0
        Repeater {
            model: 10
            delegate: RowLayout {
                id: weaponRow
                spacing: 8
                implicitHeight: 72

                readonly property string weaponShortName: hudDataModel.getWeaponShortName(index + 1)

                Image {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignVCenter
                    fillMode: Image.PreserveAspectCrop
                    mipmap: true
                    smooth: true
                    source: hudDataModel.getWeaponIconPath(index + 1)
                }

                CVarAwareCrosshairSelector {
                    // Hide natively drawn parts when running transitions
                    drawNativePart: root.StackView.view && !root.StackView.view.busy
                    nativePartOpacity: popupOverlay.visible ? 0.1 : 1.0
                    desiredWidthOrHeight: separateSizeSelector.selectedValue || -1
                    color: separateColorPicker.selectedColor || "white"
                    Layout.preferredWidth: implicitWidth
                    Layout.preferredHeight: implicitHeight
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    cvarName: "cg_crosshair_" + weaponShortName
                    model: availableCrosshairs
                }

                CVarAwareSegmentedRow {
                    id: separateSizeSelector
                    desiredSegmentWidth: 18
                    modelTitles: crosshairSizeTitles
                    modelValues: crosshairSizeValues
                    cvarName: "cg_crosshair_size_" + weaponShortName
                }

                CVarAwareColorPicker {
                    id: separateColorPicker
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: 32
                    cvarName: "cg_crosshair_color_" + weaponShortName
                }
            }
        }
    }
}
