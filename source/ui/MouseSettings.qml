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

    Column {
        anchors.centerIn: parent
        spacing: 36
        ColumnLayout {
            id: regularSettingsColumn
            width: 0.66 * root.width

            SettingsRow {
                text: "Mouse sensitivity"
                CVarAwareSlider {
                    from: 0.1
                    to: 10.0
                    cvarName: "sensitivity"
                }
            }

            SettingsRow {
                text: "Crosshair damage color"
                CVarAwareColorPicker {
                    cvarName: "cg_crosshair_damage_color"
                }
            }

            SettingsRow {
                text: "Additional strong crosshair"
                CVarAwareCrosshairSelector {
                    drawNativePart: root.isNotInTransition
                    nativePartOpacity: popupOverlay.visible ? 0.3 : 1.0
                    model: availableStrongCrosshairs
                    cvarName: "cg_crosshair_strong"
                }
            }

            SettingsRow {
                text: "Use separate settings per weapon"
                CVarAwareCheckBox {
                    id: separateSettingsCheckBox
                    cvarName: "cg_separate_weapon_settings"
                }
            }
        }

        Loader {
            id: loader
            width: 0.67 * root.width
            height: item.implicitHeight
            sourceComponent: separateSettingsCheckBox.checked ? separateCrosshairsComponent : sameCrosshairComponent
        }
    }

    Component {
        id: sameCrosshairComponent
        ColumnLayout {
            width: 0.67 * root.width
            spacing: 20

            SettingsRow {
                text: "Crosshair"
                CVarAwareCrosshairSelector {
                    drawNativePart: root.isNotInTransition
                    nativePartOpacity: popupOverlay.visible ? 0.1 : 1.0
                    desiredWidthOrHeight: sizeSelector.selectedValue || -1
                    color: colorPicker.selectedColor || "white"
                    model: availableCrosshairs
                    cvarName: "cg_crosshair"
                }
            }

            SettingsRow {
                text: "Crosshair size"
                CVarAwareSegmentedRow {
                    id: sizeSelector
                    desiredSegmentWidth: 18
                    modelTitles: crosshairSizeTitles
                    modelValues: crosshairSizeValues
                    cvarName: "cg_crosshair_size"
                }
            }

            SettingsRow {
                text: "Crosshair color"
                CVarAwareColorPicker {
                    id: colorPicker
                    cvarName: "cg_crosshair_color"
                }
            }
        }
    }

    Component {
        id: separateCrosshairsComponent
        Item {
            // Wrapping in an item is the only way to align the crosshairs column properly
            implicitWidth: root.width
            width: root.width
            implicitHeight: separateCrosshairsColumn.implicitHeight
            Rectangle {
                anchors.centerIn: separateCrosshairsColumn
                width: separateCrosshairsColumn.width + 36
                height: separateCrosshairsColumn.height + 28
                radius: 2
                color: Qt.rgba(0, 0, 0, 0.07)
            }
            ColumnLayout {
                id: separateCrosshairsColumn
                anchors.left: parent.left
                // Try making it visually fit the column
                anchors.leftMargin: 0.125 * parent.width + 4
                Repeater {
                    model: 10
                    delegate: RowLayout {
                        id: weaponRow
                        spacing: 8
                        implicitHeight: 72
                        width: 0.5 * root.width

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
    }
}
