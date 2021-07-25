import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root
    readonly property var availableCrosshairs: hudDataModel.getAvailableCrosshairs()
    readonly property var availableStrongCrosshairs: hudDataModel.getAvailableStrongCrosshairs()
    readonly property bool isNotInTransition: root.StackView.view && !root.StackView.view.busy
    readonly property color underlayColor: Qt.rgba(1.0, 1.0, 1.0, 0.07)
    readonly property color altUnderlayColor: Qt.rgba(1.0, 1.0, 1.0, 0.05)

    Column {
        anchors.centerIn: parent
        spacing: 36
        ColumnLayout {
            id: regularSettingsColumn
            width: 0.66 * root.width
            spacing: 12

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
                    desiredWidthOrHeight: strongSizeSlider.value
                    fieldWidth: wsw.maxStrongCrosshairSize
                    underlayColor: root.underlayColor
                    model: availableStrongCrosshairs
                    cvarName: "cg_crosshair_strong"
                }
            }

            SettingsRow {
                text: "Strong crosshair size"
                CVarAwareSlider {
                    id: strongSizeSlider
                    from: wsw.minStrongCrosshairSize
                    to: wsw.maxStrongCrosshairSize
                    stepSize: wsw.crosshairSizeStep
                    cvarName: "cg_crosshair_strong_size"
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
            spacing: 12

            SettingsRow {
                text: "Crosshair"
                CVarAwareCrosshairSelector {
                    drawNativePart: root.isNotInTransition
                    nativePartOpacity: popupOverlay.visible ? 0.1 : 1.0
                    desiredWidthOrHeight: sizeSlider.value
                    fieldWidth: wsw.maxRegularCrosshairSize
                    underlayColor: root.underlayColor
                    color: colorPicker.selectedColor || "white"
                    model: availableCrosshairs
                    cvarName: "cg_crosshair"
                }
            }

            SettingsRow {
                text: "Crosshair size"
                CVarAwareSlider {
                    id: sizeSlider
                    from: wsw.minRegularCrosshairSize
                    to: wsw.maxRegularCrosshairSize
                    stepSize: wsw.crosshairSizeStep
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

            ColumnLayout {
                id: separateCrosshairsColumn
                anchors.left: parent.left
                // Try making it visually fit the column
                anchors.leftMargin: 0.125 * parent.width + 4

                Repeater {
                    model: 10
                    delegate: Item {
                        implicitHeight: 54
                        width: 0.5 * root.width
                        readonly property string weaponShortName: hudDataModel.getWeaponShortName(index + 1)

                        Rectangle {
                            width: parent.width + 96
                            height: parent.height + separateCrosshairsColumn.spacing
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.horizontalCenterOffset: 14
                            anchors.verticalCenter: parent.verticalCenter
                            color: index % 2 ? root.underlayColor : root.altUnderlayColor
                        }

                        RowLayout {
                            spacing: 8
                            width: 0.5 * root.width
                            anchors.centerIn: parent

                            Image {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
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
                                desiredWidthOrHeight: separateSizeSlider.value
                                color: separateColorPicker.selectedColor || "white"
                                fieldWidth: wsw.maxRegularCrosshairSize
                                Layout.preferredWidth: implicitWidth
                                Layout.preferredHeight: implicitHeight
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                cvarName: "cg_crosshair_" + weaponShortName
                                model: availableCrosshairs
                            }

                            CVarAwareSlider {
                                id: separateSizeSlider
                                Layout.preferredWidth: 3.0 * (wsw.maxRegularCrosshairSize - wsw.minRegularCrosshairSize)
                                cvarName: "cg_crosshair_size_" + weaponShortName
                                from: wsw.minRegularCrosshairSize
                                to: wsw.maxRegularCrosshairSize
                                stepSize: wsw.crosshairSizeStep
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
}
