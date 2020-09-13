import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    ListView {
        id: list
        anchors.centerIn: parent
        width: parent.width
        height: parent.height > contentHeight ? contentHeight : parent.height
        boundsBehavior: parent.height > contentHeight ? Flickable.StopAtBounds : Flickable.OvershootBounds
        spacing: parent.height > contentHeight ? 64 : 36
        model: gametypeOptionsModel

        delegate: Item {
            id: option
            implicitWidth: loader.width
            implicitHeight: titleLabel.implicitHeight + loader.height + loader.anchors.topMargin

            readonly property int optionIndex: index
            readonly property int optionModel: model
            readonly property int optionCurrent: current

            Label {
                id: titleLabel
                anchors.top: parent.top
                width: root.width
                horizontalAlignment: Qt.AlignHCenter
                text: title
                font.weight: Font.Medium
                font.pointSize: 11
                font.letterSpacing: 1
            }

            Loader {
                id: loader
                anchors.top: titleLabel.bottom
                anchors.topMargin: kind === GametypeOptionsModel.Boolean ? 24 : 12
                anchors.horizontalCenter: parent.horizontalCenter

                readonly property int model: model
                readonly property int current: current

                width: root.width
                height: item ? item.implicitHeight : 0

                Component.onCompleted: {
                    if (kind === GametypeOptionsModel.Boolean) {
                        sourceComponent = booleanComponent
                    } else {
                        sourceComponent = selectorComponent
                    }
                }

                Component {
                    id: booleanComponent
                    RowLayout {
                        width: parent.width
                        Item {
                            Layout.fillWidth: true
                        }
                        spacing: 8
                        SelectableGametypeOption {
                            text: "On"
                            Layout.preferredWidth: 72
                            checked: option.optionCurrent === 1
                            onClicked: gametypeOptionsModel.select(option.optionIndex, 1)
                        }
                        SelectableGametypeOption {
                            text: "Off"
                            Layout.preferredWidth: 72
                            checked: option.optionCurrent === 0
                            onClicked: gametypeOptionsModel.select(option.optionIndex, 0)
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }

                Component {
                    id: selectorComponent
                    RowLayout {
                        width: root.width
                        spacing: 8
                        Item {
                            Layout.fillWidth: true
                        }
                        Repeater {
                            model: option.optionModel
                            delegate: SelectableGametypeOption {
                                Layout.preferredWidth: Math.min(root.width / option.optionCurrent, 96)
                                Material.theme: checked ? Material.Light : Material.Dark
                                checked: index === option.optionCurrent
                                iconPath: gametypeOptionsModel.getSelectorItemIcon(option.optionIndex, index)
                                text: gametypeOptionsModel.getSelectorItemTitle(option.optionIndex, index)
                                onClicked: gametypeOptionsModel.select(option.optionIndex, index)
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}