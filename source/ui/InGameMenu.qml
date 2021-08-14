import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Rectangle {
    color: wsw.colorWithAlpha(Material.background, wsw.fullscreenOverlayOpacity)

    readonly property bool canShowLoadouts: gametypeOptionsModel.available && !hudDataModel.isSpectator

    // Reserve some space for a slight default expansion of an active button
    property real tabButtonWidth: (tabBar.width - 8) / (canShowLoadouts ? 4 : 3)
    Behavior on tabButtonWidth { SmoothedAnimation { duration: 66 } }

    WswTabBar {
        id: tabBar
        visible: stackView.depth < 2
        width: mainPane.width
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        background: null

        Component.onCompleted: wsw.registerHudOccluder(tabBar)
        Component.onDestruction: wsw.unregisterHudOccluder(tabBar)
        onWidthChanged: wsw.updateHudOccluder(tabBar)
        onHeightChanged: wsw.updateHudOccluder(tabBar)
        onXChanged: wsw.updateHudOccluder(tabBar)
        onYChanged: wsw.updateHudOccluder(tabBar)

        WswTabButton {
            readonly property var component: generalComponent
            width: tabButtonWidth
            text: "General"
        }
        WswTabButton {
            readonly property var component: Component { InGameChatPage {} }
            width: tabButtonWidth
            text: "Chat"
        }
        WswTabButton {
            readonly property var component: Component { InGameCallvotesPage {} }
            width: tabButtonWidth
            text: "Callvotes"
        }
        WswTabButton {
            readonly property var component: Component { InGameGametypeOptionsPage {} }
            visible: canShowLoadouts
            width: visible ? tabButtonWidth : 0
            text: gametypeOptionsModel.tabTitle
        }

        onCurrentItemChanged: stackView.replace(currentItem.component)
    }

    Item {
        id: mainPane
        focus: true
        width: 600
        height: Math.max(600, 0.67 * parent.height)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter

        StackView {
            id: stackView
            anchors.fill: parent
            anchors.margins: 16
            initialItem: generalComponent
            clip: true
        }

        Component.onCompleted: wsw.registerHudOccluder(mainPane)
        Component.onDestruction: wsw.unregisterHudOccluder(mainPane)
        onWidthChanged: wsw.updateHudOccluder(mainPane)
        onHeightChanged: wsw.updateHudOccluder(mainPane)
        onXChanged: wsw.updateHudOccluder(mainPane)
        onYChanged: wsw.updateHudOccluder(mainPane)
    }

    Component {
        id: generalComponent
        InGameGeneralPage {}
    }

    Component {
        id: chatComponent
        InGameChatPage {}
    }

    Component {
        id: callvotesComponent
        InGameCallvotesPage {}
    }

    Component {
        id: gametypeOptionsComponent
        InGameGametypeOptionsPage {}
    }

    Connections {
        target: gametypeOptionsModel
        onAvailableChanged: {
            if (!available) {
                stackView.replace(generalComponent)
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            stackView.forceActiveFocus()
        }
    }

    onCanShowLoadoutsChanged: {
        // TODO: Is there a better approach for the forceful page selection?
        if (!canShowLoadouts) {
            tabBar.setCurrentIndex(0)
        }
    }

    Keys.onPressed: {
        if (!visible) {
            return
        }

        let currentItem = stackView.currentItem
        if (currentItem && currentItem.hasOwnProperty("handleKeyEvent")) {
            let handler = currentItem.handleKeyEvent
            if (handler && handler(event)) {
                return
            }
        }

        if (event.key !== Qt.Key_Escape) {
            return
        }

        event.accepted = true
        if (tabBar.currentIndex) {
            tabBar.currentIndex = 0
            return
        }

        if (stackView.depth === 1) {
            wsw.returnFromInGameMenu()
            return
        }

        let handler = stackView.currentItem.handleKeyBack
        if (handler && handler()) {
            return
        }

        // .pop() API quirks
        stackView.pop(stackView.get(stackView.depth - 2))
    }
}