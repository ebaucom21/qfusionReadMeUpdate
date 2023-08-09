import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Rectangle {
    color: UI.ui.colorWithAlpha(Material.background, UI.ui.fullscreenOverlayOpacity)

    readonly property bool canShowLoadouts: UI.gametypeOptionsModel.available && !UI.hudDataModel.isSpectator

    // Force redrawing stuff every frame
    ProgressBar {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        indeterminate: true
        Material.accent: parent.Material.background
    }

    CarouselTabBar {
        id: tabBar
        // Limit it to an uneven number for a nice alignment
        pathItemCount: 3
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: canShowLoadouts ? Math.min(1.25 * mainPane.width, parent.width) : mainPane.width
        height: implicitHeight
        model: canShowLoadouts ? loadoutButtonsModel : regularButtonsModel
        onCurrentIndexChanged: stackView.replace(model[currentIndex]["component"])
    }

    readonly property var regularButtonsModel: [
        {"text" : "General", "component": generalComponent},
        {"text" : "Chat", "component" : chatComponent},
        {"text" : "Callvotes", "component" : callvotesComponent},
    ]

    readonly property var loadoutButtonsModel: [
        {"text" : "General", "component": generalComponent},
        {"text" : "Chat", "component" : chatComponent},
        {"text" : UI.gametypeOptionsModel.tabTitle, "component" : gametypeOptionsComponent},
        {"text" : "Callvotes", "component" : callvotesComponent},
    ]

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

        Component.onCompleted: UI.ui.registerHudOccluder(mainPane)
        Component.onDestruction: UI.ui.unregisterHudOccluder(mainPane)
        onWidthChanged: UI.ui.updateHudOccluder(mainPane)
        onHeightChanged: UI.ui.updateHudOccluder(mainPane)
        onXChanged: UI.ui.updateHudOccluder(mainPane)
        onYChanged: UI.ui.updateHudOccluder(mainPane)
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
        target: UI.gametypeOptionsModel
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

    Keys.onPressed: {
        if (visible) {
            const currentItem = stackView.currentItem
            if (currentItem) {
                // Check if the current item can handle the event on its own
                const handler = currentItem["handleKeyEvent"]
                if (handler && handler(event)) {
                    return
                }
                if (event.key === Qt.Key_Escape) {
                    event.accepted = true
                    // Check if the current item can handle back navigation on its own
                    const handler = currentItem["handleKeyBack"]
                    if (handler && handler(event)) {
                        return
                    }
                    if (tabBar.currentIndex) {
                        tabBar.currentIndex = 0
                    } else {
                        UI.ui.returnFromInGameMenu()
                    }
                }
            }
        }
    }
}