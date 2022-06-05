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
    readonly property real innerPaneMargin: 20.0
    readonly property real innerPaneWidth: 0.5 * root.width - innerPaneMargin

    Column {
        anchors.centerIn: parent
        spacing: 36
        ColumnLayout {
            id: regularSettingsColumn
            width: 0.66 * root.width
            spacing: 12

            SettingsRow {
                text: "Mouse sensitivity"
                CVarAwareSliderWithBox {
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
                CVarAwareSliderWithBox {
                    id: strongSizeSlider
                    from: wsw.minStrongCrosshairSize
                    to: wsw.maxStrongCrosshairSize
                    stepSize: wsw.crosshairSizeStep
                    cvarName: "cg_crosshair_strong_size"
                    fractionalPartDigits: 0
                }
            }

            SettingsRow {
                text: "Use separate settings per weapon"
                CVarAwareCheckBox {
                    id: separateSettingsCheckBox
                    cvarName: "cg_separate_weapon_settings"
                }
            }

            SettingsLabel {
                text: "Weapon-specific settings"
                Layout.topMargin: 16
                Layout.maximumWidth: 99999
                horizontalAlignment: Qt.AlignHCenter
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
            opacity: 0.0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Component.onCompleted: opacity = 1.0

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
                CVarAwareSliderWithBox {
                    id: sizeSlider
                    from: wsw.minRegularCrosshairSize
                    to: wsw.maxRegularCrosshairSize
                    stepSize: wsw.crosshairSizeStep
                    cvarName: "cg_crosshair_size"
                    fractionalPartDigits: 0
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
            id: separateCrosshairsPane
            implicitWidth: root.width
            width: root.width
            implicitHeight: Math.max(weaponsList.height, detailsPane.height)
            opacity: 0.0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Component.onCompleted: opacity = 1.0

            property int selectedIndex: 0
            property string weaponShortName: hudDataModel.getWeaponShortName(selectedIndex + 1)

            ListView {
                id: weaponsList
                width: innerPaneWidth
                height: contentHeight
                model: 10
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.horizontalCenter
                anchors.rightMargin: innerPaneMargin
                interactive: false
                delegate: Label {
                    width: innerPaneWidth
                    height: 36
                    horizontalAlignment: Qt.AlignRight
                    verticalAlignment: Qt.AlignVCenter
                    font.pointSize: 12
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: mouseArea.containsMouse ? 1.75 : 1.25
                    Behavior on font.letterSpacing { NumberAnimation { duration: 67 } }
                    color: (mouseArea.containsMouse || selectedIndex === index) ? Material.accent : Material.foreground
                    text: hudDataModel.getWeaponFullName(index + 1)
                    MouseArea {
                        id: mouseArea
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        width: parent.implicitWidth
                        height: parent.height
                        hoverEnabled: true
                        onClicked: selectedIndex = index
                    }
                }
            }

            ColumnLayout {
                id: detailsPane
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.horizontalCenter
                anchors.leftMargin: innerPaneMargin
                width: innerPaneWidth
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 192
                    Layout.preferredHeight: 192
                    Layout.alignment: Qt.AlignLeft
                    Layout.leftMargin: 12
                    Layout.bottomMargin: 32
                    color: root.underlayColor

                    NativelyDrawnModel {
                        visible: root.isNotInTransition
                        anchors.fill: parent
                        modelName: hudDataModel.getWeaponModelPath(selectedIndex + 1)
                        viewOrigin: Qt.vector3d(48.0, 0.0, 20.0)
                        modelOrigin: Qt.vector3d(0.0, 0.0, 0.0)
                        desiredModelHeight: 16.0
                        rotationSpeed: -60.0
                        outlineHeight: 0.5
                    }
                }

                CVarAwareCrosshairSelector {
                    drawNativePart: root.isNotInTransition
                    nativePartOpacity: popupOverlay.visible ? 0.1 : 1.0
                    desiredWidthOrHeight: separateSizeSlider.value
                    color: separateColorPicker.selectedColor || "white"
                    fieldWidth: wsw.maxRegularCrosshairSize
                    Layout.preferredWidth: implicitWidth
                    Layout.preferredHeight: implicitHeight
                    underlayColor: root.underlayColor
                    cvarName: "cg_crosshair_" + weaponShortName
                    model: availableCrosshairs
                }

                CVarAwareSliderWithBox {
                    id: separateSizeSlider
                    Layout.alignment: Qt.AlignLeft
                    cvarName: "cg_crosshair_size_" + weaponShortName
                    from: wsw.minRegularCrosshairSize
                    to: wsw.maxRegularCrosshairSize
                    stepSize: wsw.crosshairSizeStep
                    fractionalPartDigits: 0
                }

                CVarAwareColorPicker {
                    id: separateColorPicker
                    Layout.alignment: Qt.AlignLeft
                    cvarName: "cg_crosshair_color_" + weaponShortName
                }
            }
        }
    }
}
