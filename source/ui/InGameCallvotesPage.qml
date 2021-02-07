import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    StackView {
        id: stackView
        width: root.width - 16
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        initialItem: voteSelectionComponent
    }

    property int selectedVoteIndex
    property string selectedVoteName
    property string selectedVoteDesc
    property int selectedVoteFlags
    property int selectedVoteArgsHandle
    property int selectedVoteArgsKind
    property string selectedVoteCurrent
    property var selectedVoteChosen

    property var activeCallvotesModel: wsw.isOperator ? operatorCallvotesModel : regularCallvotesModel

    Connections {
        target: activeCallvotesModel
        onCurrentChanged: {
            if (index === selectedVoteIndex) {
                selectedVoteCurrent = value
            }
        }
    }

    Component {
        id: voteSelectionComponent
        ListView {
            id: list

            model: activeCallvotesModel
            spacing: 12

            readonly property int stage: 1

            delegate: SelectableLabel {
                text: name
                width: list.width
                horizontalAlignment: Qt.AlignHCenter
                onClicked: {
                    selectedVoteIndex = index
                    selectedVoteName = name
                    selectedVoteDesc = desc
                    selectedVoteFlags = flags
                    selectedVoteArgsHandle = argsHandle
                    selectedVoteArgsKind = argsKind
                    selectedVoteCurrent = current
                    stackView.replace(argsSelectionComponent)
                }
            }
        }
    }

    Component {
        id: argsSelectionComponent

        ColumnLayout {
            spacing: 20

            readonly property int stage: 2

            Component.onCompleted: {
                if (selectedVoteArgsKind === CallvotesModel.Boolean) {
                    argsSelectionLoader.sourceComponent = booleanComponent
                } else if (selectedVoteArgsKind === CallvotesModel.Number) {
                    argsSelectionLoader.sourceComponent = numberComponent
                } else if (selectedVoteArgsKind === CallvotesModel.Player) {
                    argsSelectionLoader.sourceComponent = playerComponent
                } else if (selectedVoteArgsKind === CallvotesModel.Minutes) {
                    argsSelectionLoader.sourceComponent = minutesComponent
                } else if (selectedVoteArgsKind === CallvotesModel.Options) {
                    argsSelectionLoader.sourceComponent = optionsComponent
                } else if (selectedVoteArgsKind === CallvotesModel.MapList){
                    argsSelectionLoader.sourceComponent = mapListComponent
                } else {
                    argsSelectionLoader.sourceComponent = noArgsComponent
                }
            }

            Component {
                id: booleanComponent
                InGameCallvoteScalarSelector {
                    currentValue: selectedVoteCurrent
                    valueFormatter: v => v ? "YES" : "NO"
                    delegate: RowLayout {
                        spacing: 16
                        SelectableLabel {
                            Layout.preferredWidth: implicitWidth
                            Layout.preferredHeight: 32
                            enabled: currentValue != 1
                            selected: chosenValue == 1
                            text: "yes"
                            onClicked: chosenValue = 1
                        }
                        SelectableLabel {
                            Layout.preferredWidth: implicitWidth
                            Layout.preferredHeight: 32
                            enabled: currentValue != 0
                            selected: chosenValue == 0
                            text: "no"
                            onClicked: chosenValue = 0
                        }
                    }
                }
            }

            Component {
                id: numberComponent
                InGameCallvoteNumberSelector {
                    currentValue: selectedVoteCurrent
                }
            }

            Component {
                id: minutesComponent
                InGameCallvoteNumberSelector {
                    currentValue: selectedVoteCurrent
                    valueFormatter: v => v === 1 ? v + " minute" : v + " minutes"
                }
            }

            Component {
                id: playerComponent
                InGameCallvotePlayerSelector {
                    currentValue: selectedVoteCurrent
                }
            }

            Component {
                id: optionsComponent
                InGameCallvoteOptionSelector {
                    currentValue: selectedVoteCurrent
                    model: activeCallvotesModel.getOptionsList(selectedVoteArgsHandle)
                }
            }

            Component {
                id: mapListComponent
                InGameCallvoteMapSelector {
                    currentValue: selectedVoteCurrent
                    model: activeCallvotesModel.getOptionsList(selectedVoteArgsHandle)
                }
            }

            Component {
                id: noArgsComponent
                Label {
                    readonly property bool canProceed: true
                    readonly property string chosenValue: ""
                    readonly property string displayedString: ""
                    horizontalAlignment: Qt.AlignHCenter
                    verticalAlignment: Qt.AlignVCenter
                    font.pointSize: 11
                    font.letterSpacing: 1
                    text: "This vote has no arguments"
                }
            }

            Label {
                Layout.preferredWidth: root.width
                horizontalAlignment: Qt.AlignHCenter
                font.pointSize: 16
                font.capitalization: Font.AllUppercase
                font.weight: Font.Medium
                text: selectedVoteName
            }
            Label {
                Layout.preferredWidth: root.width
                horizontalAlignment: Qt.AlignHCenter
                wrapMode: Text.WordWrap
                font.pointSize: 11
                font.letterSpacing: 1
                text: selectedVoteDesc
            }

            Loader {
                id: argsSelectionLoader
                Layout.topMargin: 16
                Layout.bottomMargin: 16
                Layout.preferredWidth: root.width
                Layout.fillHeight: true
            }

            Label {
                id: descLabel
                visible: selectedVoteArgsKind !== CallvotesModel.NoArgs
                Layout.preferredWidth: root.width
                horizontalAlignment: Qt.AlignHCenter
                font.pointSize: 11
                font.letterSpacing: 0.5
                text: argsSelectionLoader.item.displayedString
            }

            RowLayout {
                Layout.preferredWidth: root.width
                Button {
                    flat: true
                    text: "back"
                    Layout.preferredWidth: 64
                    Material.theme: Material.Dark
                    onClicked: stackView.replace(voteSelectionComponent)
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    visible: wsw.isOperator && (selectedVoteFlags & CallvotesModel.Operator)
                    enabled: argsSelectionLoader.item && argsSelectionLoader.item.canProceed
                    flat: true
                    highlighted: !voteButton.visible
                    text: "opcall"
                    Layout.preferredWidth: 72
                    Material.theme: Material.Dark
                    onClicked: startVote(argsSelectionLoader.item.chosenValue, true)
                }
                Button {
                    id: voteButton
                    visible: selectedVoteFlags & CallvotesModel.Regular
                    enabled: argsSelectionLoader.item && argsSelectionLoader.item.canProceed
                    flat: true
                    highlighted: enabled
                    text: "vote"
                    Layout.preferredWidth: 72
                    Material.theme: enabled ? Material.light : Material.Dark
                    onClicked: startVote(argsSelectionLoader.item.chosenValue, false)
                }
            }
        }
    }

    function startVote(chosenValue, isOperatorCall) {
        wsw.callVote(selectedVoteName, chosenValue, isOperatorCall)
        stackView.replace(voteSelectionComponent)
        wsw.returnFromInGameMenu()
    }

    function handleKeyEvent(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            if (stackView.currentItem.stage === 2) {
                stackView.replace(voteSelectionComponent)
                event.accepted = true
                return true
            }
        }
        return false
    }
}