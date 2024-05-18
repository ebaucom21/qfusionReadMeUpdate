import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
	id: root

    SequentialAnimation {
        running: true
        loops: Animation.Infinite
        NumberAnimation {
            target: radialGradient
            property: "horizontalRadius"
            from: parent.width
            to: 0.33 * parent.width
            duration: 10000
        }
        NumberAnimation {
            target: radialGradient
            property: "horizontalRadius"
            from: 0.33 * parent.width
            to: parent.width
            duration: 10000
        }
    }

    Rectangle {
        visible: !UI.ui.isClientDisconnected
        anchors.fill: parent
        color: UI.ui.colorWithAlpha(Material.background, UI.ui.fullscreenOverlayOpacity)
    }

    RadialGradient {
        id: radialGradient
        visible: UI.ui.isClientDisconnected
        anchors.fill: parent
        horizontalRadius: parent.width
        // Stretches it vertically making it almost a column in the expanded state
        verticalRadius: parent.height * (1.0 + 3.0 * primaryMenu.expansionFrac)
        gradient: Gradient {
            GradientStop {
                position: 0.00
                color: UI.ui.colorWithAlpha(Qt.tint(Material.background, UI.ui.colorWithAlpha(Material.accent, 0.05)), 0.99)
            }
            GradientStop {
                position: 1.00
	            // The gradient makes it look denser so the base value is slightly lower
                color: UI.ui.colorWithAlpha(Qt.darker(Material.backgroundColor, 1.2), UI.ui.fullscreenOverlayOpacity - 0.10)
            }
        }
    }

	MainMenuPrimaryMenu {
		id: primaryMenu
		anchors.centerIn: parent
		shouldShowExpandedButtons: parent.width >= 2400 && (parent.width / parent.height) >= 2.0
        width: parent.width + (shouldShowExpandedButtons ? 0 : 2 * (UI.ui.mainMenuButtonWidthDp + UI.ui.mainMenuButtonTrailWidthDp))
        height: parent.height
	}

	Component {
	    id: newsComponent
	    NewsPage {}
	}

	Component {
	    id: profileComponent
	    ProfilePage {}
	}

	Component {
	    id: playOnlineComponent
	    PlayOnlinePage {}
	}

	Component {
	    id: localGameComponent
	    LocalGamePage {}
	}

    Component {
        id: settingsComponent
        SettingsPage {}
    }

    Component {
        id: demosComponent
        DemosPage {}
    }

    Component {
        id: helpComponent
        HelpPage {}
    }

    Component {
        id: quitComponent
        QuitPage {
            backTrigger: () => {
                primaryMenu.handleKeyBack()
            }
        }
    }

    StackView {
		id: contentPane
		hoverEnabled: primaryMenu.expansionFrac >= 1.0
		opacity: primaryMenu.expansionFrac
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		anchors.horizontalCenter: parent.horizontalCenter
		width: 1024
	}

    Connections {
        target: primaryMenu
        onActivePageTagChanged: {
            let tag = primaryMenu.activePageTag
            if (!tag) {
                contentPane.clear()
                return
            }
            if (tag === primaryMenu.pageNews) {
                contentPane.replace(newsComponent)
            } else if (tag === primaryMenu.pageProfile) {
                contentPane.replace(profileComponent)
            } else if (tag === primaryMenu.pagePlayOnline) {
                contentPane.replace(playOnlineComponent)
            } else if (tag === primaryMenu.pageLocalGame) {
                contentPane.replace(localGameComponent)
            } else if (tag === primaryMenu.pageSettings) {
                contentPane.replace(settingsComponent)
            } else if (tag === primaryMenu.pageDemos) {
                contentPane.replace(demosComponent)
            } else if (tag === primaryMenu.pageHelp) {
                contentPane.replace(helpComponent)
            } else if (tag === primaryMenu.pageQuit) {
                contentPane.replace(quitComponent)
            }
            contentPane.currentItem.forceActiveFocus()
        }
    }

	Keys.onPressed: {
	    if (!visible) {
	        return
	    }
	    let currentPaneItem = contentPane.currentItem
	    // TODO: Events propagation needs some attention and some work, e.g. setting the .accepted flag
	    // TODO: Check whether Keys.redirectTo is applicable
	    if (currentPaneItem) {
	        if (currentPaneItem.hasOwnProperty("handleKeyEvent")) {
	            let handler = currentPaneItem.handleKeyEvent
	            if (handler && handler(event)) {
	                return
	            }
	        }
	    }
	    if (primaryMenu.handleKeyEvent(event)) {
            return
        }

	    if (event.key !== Qt.Key_Escape) {
	        return
	    }
	    UI.ui.playBackSound()
	    UI.ui.returnFromMainMenu()
	    root.forceActiveFocus()
	    event.accepted = true
	}
}