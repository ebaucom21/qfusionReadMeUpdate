import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtMultimedia 5.13
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
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
        text: swipeView.currentItem.subpageTitle
    }

    property var selectedGametypeTitle
    property var selectedGametypeName
    property var gametypeMapsList
    property var selectedMapName
    property var selectedMapTitle

    SwipeView {
        id: swipeView
        clip: true
        interactive: false
        width: 0.75 * parent.width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: titleLabel.bottom
        anchors.topMargin: 32
        anchors.bottom: buttonsBar.top
        anchors.bottomMargin: 32

        LocalGameDetailArrangement {
            id: gametypePage
            readonly property string subpageTitle: "Step <b>1/3</b> - Select a gametype"
            readonly property bool canGoPrev: false
            readonly property bool canGoNext: detailed

            desiredWidth: swipeView.width
            desiredHeight: swipeView.height

            listComponent: ListView {
                id: gametypesListView
                model: gametypesModel
                interactive: false
                delegate: LocalGameListDelegate {
                    detailed: gametypePage.detailed
                    width: gametypePage.expectedListItemWidth
                    text: title
                    selected: index === gametypePage.selectedIndex
                    onClicked: gametypePage.selectedIndex = index
                    // Handles external selectedIndex changes as well
                    onSelectedChanged: {
                        if (selected) {
                            selectProps()
                        }
                    }
                    function selectProps() {
                        gametypePage.detailed = true
                        selectedGametypeTitle = title
                        selectedGametypeName = name
                        gametypeMapsList = maps
                    }
                }
            }

            detailComponent: ColumnLayout {
                Rectangle {
                    width: gametypePage.expectedDetailsWidth
                    Layout.preferredHeight: gametypePage.detailsVCenterOffset
                }
            }
        }

        LocalGameDetailArrangement {
            id: mapPage
            readonly property string subpageTitle: "Step <b>2/3</b> - Select a map"
            readonly property bool canGoPrev: true
            readonly property bool canGoNext: detailed

            desiredWidth: swipeView.width
            desiredHeight: swipeView.height

            listComponent: ListView {
                interactive: false
                model: gametypeMapsList
                delegate: LocalGameListDelegate {
                    detailed: mapPage.detailed
                    width: mapPage.expectedListItemWidth
                    text: modelData["title"]
                    selected: index === mapPage.selectedIndex
                    onClicked: mapPage.selectedIndex = index
                    // Handles external selectedIndex changes as well
                    onSelectedChanged: {
                        if (selected) {
                            selectProps()
                        }
                    }
                    function selectProps() {
                        mapPage.detailed = true
                        selectedMapName = modelData["name"]
                        selectedMapTitle = modelData["title"]
                    }
                }
            }

            detailComponent: ColumnLayout {
                Rectangle {
                    width: gametypePage.expectedDetailsWidth
                    Layout.preferredHeight: gametypePage.detailsVCenterOffset
                }
            }
        }

        Item {
            readonly property string subpageTitle: "Step <b>3/3</b> - Set rules"
            readonly property bool canGoPrev: true
            readonly property bool canGoNext: false
        }
    }

    PageIndicator {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: buttonsBar.top
        anchors.bottomMargin: 72
        count: 3
        currentIndex: swipeView.currentIndex
        interactive: false
    }

    RowLayout {
        id: buttonsBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32

        Item {
            Layout.fillWidth: true
        }

        Button {
            flat: true
            text: "back"
            Layout.preferredWidth: 120
            visible: swipeView.currentItem.canGoPrev
            onClicked: {
                swipeView.currentIndex = swipeView.currentIndex - 1
            }
        }

        Item { Layout.fillWidth: true }

        Button {
            highlighted: true
            text: "next"
            Layout.preferredWidth: 120
            visible: swipeView.currentItem.canGoNext
            onClicked: {
                swipeView.currentIndex = swipeView.currentIndex + 1
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }

    function handleBackEvent(event) {
        const index = swipeView.currentIndex
        if (index === 0) {
            if (gametypePage.detailed) {
                gametypePage.detailed = false
                gametypePage.selectedIndex = -1
                event.accepted = true
                return true
            }
            return false
        }
        if (index === 1) {
            if (mapPage.detailed) {
                mapPage.detailed = false
                mapPage.selectedIndex = -1
            } else {
                swipeView.currentIndex = 0
            }
            event.accepted = true
            return true
        }
        swipeView.currentIndex = 1
        event.accepted = true
        return true
    }

    function handleCycleList(event, step) {
        const index = swipeView.currentIndex
        if (index === 0 && gametypePage.detailed) {
            gametypePage.selectPrevOrNext(step)
        }
        if (index === 1 && mapPage.detailed) {
            mapPage.selectPrevOrNext(step)
        }
        // Always consider it handled
        event.accepted = true
        return true
    }

    function handleKeyEvent(event) {
        const key = event.key
        if (key === Qt.Key_Escape || key == Qt.Key_Back) {
            return handleBackEvent(event)
        }
        if (key === Qt.Key_Left && swipeView.currentItem.canGoPrev) {
            return handleBackEvent(event)
        }
        if (key === Qt.Key_Right && swipeView.currentItem.canGoNext) {
            swipeView.currentIndex = swipeView.currentIndex + 1
            event.accepted = true
            return
        }
        if (key === Qt.Key_Up) {
            return handleCycleList(event, -1)
        }
        if (key === Qt.Key_Down) {
            return handleCycleList(event, +1)
        }
        // TODO: Accept ENTER key on the last page
        // TODO: Accept ENTER keys to continue
        // TODO: Accept TAB keys
        return false
    }
}