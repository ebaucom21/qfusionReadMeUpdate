import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root
    property var selectedForLoadingFileName
    property var selectedForSavingFileName
    signal exitRequested()

    readonly property var existingHuds: hudEditorLayoutModel.existingHuds
    readonly property real listItemWidth: 300

    Label {
        id: titleLabel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 32
        anchors.bottomMargin: 32
        horizontalAlignment: Qt.AlignHCenter
        font.weight: Font.Medium
        font.pointSize: 12
        font.letterSpacing: 1
        text: "Step <b>" + (swipeView.currentIndex + 1) + "/3</b> - " + swipeView.currentItem.subpageTitle
    }

    Label {
        id: summaryLabel
        anchors.top: titleLabel.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        horizontalAlignment: Qt.AlignHCenter
        width: parent.width
        maximumLineCount: 1
        elide: Qt.ElideRight
        font.pointSize: 12
        font.letterSpacing: 1
        text: swipeView.currentItem.summary
    }

    SwipeView {
        id: swipeView
        anchors.top: summaryLabel.bottom
        anchors.bottom: buttonsBar.top
        anchors.left: parent.left
        anchors.right: parent.right
        interactive: false
        clip: true

        onCurrentIndexChanged: {
            selectedForSavingFileName = undefined
            if (swipeView.currentIndex === 0) {
                selectedForLoadingFileName = undefined
            }
        }

        Item {
            id: loadPage
            readonly property string subpageTitle: "Select a HUD file to load"
            readonly property string summary: {
                if (selectedForLoadingFileName) {
                    'You have selected loading of the <b><font color="yellow">' +
                        selectedForLoadingFileName + "</font></b> HUD"
                } else {
                    ''
                }
            }
            readonly property bool canGoPrev: false
            readonly property bool canGoNext: !!selectedForLoadingFileName
            property int selectedIndex: -1
            width: root.width

            SwipeView.onIsCurrentItemChanged: {
                if (SwipeView.isCurrentItem) {
                    loadPage.selectedIndex = -1
                }
            }

            Flickable {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: listItemWidth
                Item {
                    width: listItemWidth
                    height: Math.max(parent.height, selectHudToLoadList.height)
                    ListView {
                        id: selectHudToLoadList
                        width: listItemWidth
                        height: contentHeight
                        anchors.centerIn: parent
                        spacing: 12
                        model: existingHuds
                        delegate: SelectableLabel {
                            width: listItemWidth
                            text: modelData
                            selected: loadPage.selectedIndex === index
                            onClicked: {
                                selectedForLoadingFileName = modelData
                                loadPage.selectedIndex = index
                            }
                        }
                    }
                }
            }
        }

        Item {
            readonly property string subpageTitle: "Edit the selected HUD"
            readonly property string summary: '<b>' + (selectedForLoadingFileName || '').toUpperCase() + '</b>'
            readonly property bool canGoPrev: true
            readonly property bool canGoNext: true
            width: root.width

            HudEditorField {
                id: editorField
                anchors.centerIn: parent
                width: 0.8 * parent.width
                height: (9.0 / 16.0) * editorField.width
                Component.onCompleted: hudEditorLayoutModel.setFieldSize(width, height)
            }
        }

        Item {
            id: savePage
            readonly property string subpageTitle: 'Select a file to save the edited HUD'
            readonly property string summary: {
                if (selectedForSavingFileName) {
                    if (selectedForSavingFileName === selectedForLoadingFileName) {
                        'You have selected saving changes back to the <b><font color="yellow">' +
                            selectedForLoadingFileName + '</font></b> HUD file'
                    } else if (savePage.selectedIndex == existingHuds.length) {
                        'You have selected saving the edited <b><font color="yellow">' + selectedForLoadingFileName +
                        '</font></b> HUD to <b><font color="yellow">' + selectedForSavingFileName + '</font><b>'
                    } else {
                        '<b><font color="red">Caution: </font></b>' +
                        'You have selected overwriting an unrelated existing file by an edited ' +
                        '<b><font color="yellow">' + selectedForLoadingFileName + '</b></font> HUD'
                    }
                } else {
                    ''
                }
            }
            readonly property bool canGoPrev: true
            readonly property bool canGoNext: !!selectedForSavingFileName
            property int selectedIndex: -1
            width: root.width

            SwipeView.onIsCurrentItemChanged: {
                if (SwipeView.isCurrentItem) {
                    savePage.selectedIndex = -1
                }
            }

            onSelectedIndexChanged: {
                if (savePage.selectedIndex >= 0) {
                    // The model could differ from the default/existing one
                    selectedForSavingFileName = selectHudToSaveList.model[savePage.selectedIndex]
                } else {
                    selectedForSavingFileName = undefined
                }
            }

            WswCheckBox {
                id: allowOverwritingCheckBox
                anchors.top: parent.top
                anchors.topMargin: 16
                anchors.left: parent.left
                anchors.leftMargin: width / 2
                Material.theme: checked ? Material.Light : Material.Dark
                text: "Allow overwriting existing files"
                onCheckedChanged: {
                    if (!checked && savePage.selectedIndex >= 0) {
                        if (savePage.selectedIndex !== existingHuds.length) {
                            savePage.selectedIndex = -1
                        }
                    }
                }
            }

            Flickable {
                anchors.top: allowOverwritingCheckBox.bottom
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: listItemWidth
                Item {
                    width: listItemWidth
                    height: Math.max(parent.height, selectHudToSaveColumn.height)
                    Column {
                        id: selectHudToSaveColumn
                        anchors.centerIn: parent
                        spacing: 8
                        ListView {
                            id: selectHudToSaveList
                            model: existingHuds
                            width: listItemWidth
                            height: contentHeight
                            spacing: 12
                            delegate: SelectableLabel {
                                enabled: allowOverwritingCheckBox.checked || index === existingHuds.length
                                width: listItemWidth
                                text: modelData
                                selected: savePage.selectedIndex === index
                                onClicked: {
                                    savePage.selectedIndex = index
                                    // Remove the no longer useful custom name by switching back to the default model
                                    if (model.length != existingHuds.length && index != existingHuds.length) {
                                        selectHudToSaveList.model = existingHuds
                                    }
                                }
                            }
                        }
                        AddNewListItemButton {
                            width: listItemWidth
                            height: 36
                            onAdditionRequested: {
                                const model = [...existingHuds]
                                model.push(text)
                                selectHudToSaveList.model = model
                                savePage.selectedIndex = model.length - 1
                                // Must do that manually since the index could remain the same
                                selectedForSavingFileName = text
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.centerIn: pageIndicator
        width: pageIndicator.width + 48
        height: pageIndicator.height + 20
        radius: 5
        color: Qt.rgba(0, 0, 0, 0.1)
    }

    PageIndicator {
        id: pageIndicator
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: buttonsBar.anchors.bottomMargin + 16
        count: 3
        currentIndex: swipeView.currentIndex
        interactive: false
    }

    RowLayout {
        id: buttonsBar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32
        width: 0.67 * parent.width
        height: 64

        Button {
            id: prevButton
            flat: true
            text: "back"
            Layout.preferredWidth: 120
            visible: swipeView.currentItem.canGoPrev
            onClicked: swipeView.currentIndex = swipeView.currentIndex - 1
        }

        Item {
            Layout.preferredWidth: prevButton.Layout.preferredWidth
            visible: !prevButton.visible
        }

        Item { Layout.fillWidth: true }

        Button {
            id: nextButton
            highlighted: true
            text: swipeView.currentIndex === 2 ? "save" : "next"
            Layout.preferredWidth: 120
            visible: swipeView.currentItem.canGoNext
            onClicked: {
                if (swipeView.currentIndex === 0) {
                    if (hudEditorLayoutModel.load(selectedForLoadingFileName)) {
                        swipeView.currentIndex = 1
                    } else {
                        // TODO: What to do? Shake?
                    }
                } else if (swipeView.currentIndex === 1) {
                    swipeView.currentIndex = 2
                } else {
                    if (hudEditorLayoutModel.save(selectedForSavingFileName)) {
                        root.exitRequested()
                    } else {
                        // TODO: Shake?
                        swipeView.currentIndex = 0
                    }
                }
            }
        }

        Item {
            Layout.preferredWidth: prevButton.Layout.preferredWidth
            visible: !nextButton.visible
        }
    }

    function handleKeyEvent(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            if (swipeView.currentIndex) {
                swipeView.currentIndex = swipeView.currentIndex - 1
                event.accepted = true
                return true
            }
        }
        return false
    }
}