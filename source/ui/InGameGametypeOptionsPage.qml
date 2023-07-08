import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    property bool useOptionCards: false
    readonly property real optionWidth: useOptionCards ? 72 + 16 : 72 + 24
    readonly property real optionSpacing: useOptionCards ? 12 : 18
    readonly property real maxOptionsPerRow: useOptionCards ? 5 : 3

    ToolButton {
        anchors.right: parent.right
        anchors.top: parent.top
        text: "Switch"
        checkable: false
        onClicked: useOptionCards = !useOptionCards
    }

    readonly property var booleanOptionTexts: ["Off", "On"]
    readonly property var booleanOptionVals: [0, 1]
    readonly property var booleanOptionPredicates: [(x) => x === 0, (x) => x !== 0]
    readonly property var booleanOptionIcons: ["image://wsw/gfx/hud/icons/vsay/no", "image://wsw/gfx/hud/icons/vsay/yes"]

    ListView {
        id: list
        anchors.centerIn: parent
        width: parent.width
        height: parent.height > contentHeight ? contentHeight : parent.height
        boundsBehavior: parent.height > contentHeight ? Flickable.StopAtBounds : Flickable.OvershootBounds
        spacing: parent.height > contentHeight ? 48 : 36
        model: UI.gametypeOptionsModel

        delegate: Item {
            id: option
            implicitWidth: loader.width
            implicitHeight: titleLabel.implicitHeight + loader.height + loader.anchors.topMargin

            readonly property int optionRow: index
            readonly property int optionModel: model
            readonly property int optionCurrent: current

            Label {
                id: titleLabel
                anchors.top: parent.top
                width: root.width
                horizontalAlignment: Qt.AlignHCenter
                text: title
                font.weight: Font.Medium
                font.pointSize: 13
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1
            }

            Loader {
                id: loader
                anchors.top: titleLabel.bottom
                anchors.topMargin: 24
                anchors.horizontalCenter: parent.horizontalCenter

                width: root.width
                height: item ? item.implicitHeight : 0

                sourceComponent: (kind === GametypeOptionsModel.Boolean) ? booleanComponent : selectorComponent

                Component {
                    id: booleanComponent
                    RowLayout {
                        width: root.width
                        spacing: optionSpacing

                        Item { Layout.fillWidth: true }

                        Repeater {
                            model: 2
                            delegate: useOptionCards ? booleanCardOption : booleanSlantedOption
                        }

                        Item { Layout.fillWidth: true }

                        Component {
                            id: booleanCardOption
                            GametypeCardOption {
                                text: booleanOptionTexts[index]
                                Layout.preferredWidth: optionWidth
                                checked: booleanOptionPredicates[index](option.optionCurrent)
                                iconPath: booleanOptionIcons[index]
                                onClicked: UI.gametypeOptionsModel.select(option.optionRow, booleanOptionVals[index])
                            }
                        }

                        Component {
                            id: booleanSlantedOption
                            GametypeSlantedOption {
                                text: booleanOptionTexts[index]
                                Layout.preferredWidth: optionWidth
                                checked: booleanOptionPredicates[index](option.optionCurrent)
                                iconPath: booleanOptionIcons[index]
                                onClicked: UI.gametypeOptionsModel.select(option.optionRow, booleanOptionVals[index])
                            }
                        }
                    }
                }

                Component {
                    id: selectorComponent
                    ColumnLayout {
                        spacing: 24
                        width: root.width
                        Repeater {
                            model: Math.max(option.optionModel / maxOptionsPerRow, 1)
                            RowLayout {
                                readonly property int rowIndex: index

                                width: root.width
                                spacing: optionSpacing

                                Item { Layout.fillWidth: true }

                                Repeater {
                                    model: rowIndex != Math.floor(option.optionModel / maxOptionsPerRow) ?
                                        maxOptionsPerRow : option.optionModel % maxOptionsPerRow
                                    delegate: useOptionCards ? selectorCardOption : selectorSlantedOption
                                }

                                Item { Layout.fillWidth: true }

                                Component {
                                    id: selectorCardOption
                                    GametypeCardOption {
                                        readonly property int flatIndex: rowIndex * maxOptionsPerRow + index
                                        checked: flatIndex === option.optionCurrent
                                        iconPath: UI.gametypeOptionsModel.getSelectorItemIcon(option.optionRow, flatIndex)
                                        text: UI.gametypeOptionsModel.getSelectorItemTitle(option.optionRow, flatIndex)
                                        onClicked: UI.gametypeOptionsModel.select(option.optionRow, flatIndex)
                                    }
                                }

                                Component {
                                    id: selectorSlantedOption
                                    GametypeSlantedOption {
                                        readonly property int flatIndex: rowIndex * maxOptionsPerRow + index
                                        checked: flatIndex === option.optionCurrent
                                        iconPath: UI.gametypeOptionsModel.getSelectorItemIcon(option.optionRow, flatIndex)
                                        text: UI.gametypeOptionsModel.getSelectorItemTitle(option.optionRow, flatIndex)
                                        onClicked: UI.gametypeOptionsModel.select(option.optionRow, flatIndex)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}