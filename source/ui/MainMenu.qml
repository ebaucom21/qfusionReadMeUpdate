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

    RadialGradient {
        id: radialGradient
        anchors.fill: parent
        horizontalRadius: parent.width
        // Stretches it vertically making it almost a column in the expanded state
        verticalRadius: parent.height * (1.0 + 3.0 * centralOverlay.expansionFrac)
        gradient: Gradient {
            GradientStop {
                position: 0.00
                color: wsw.colorWithAlpha(Qt.tint(Material.background, wsw.colorWithAlpha(Material.accent, 0.05)), 0.99)
            }
            GradientStop {
                position: 1.00
	            // The gradient makes it look denser so the base value is slightly lower
                color: wsw.colorWithAlpha(Material.backgroundColor, wsw.fullscreenOverlayOpacity - 0.05)
            }
        }
    }

	CentralOverlayGroup {
		id: centralOverlay
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
                centralOverlay.handleKeyBack()
            }
        }
    }

    StackView {
		id: contentPane
		hoverEnabled: centralOverlay.expansionFrac >= 1.0
		opacity: centralOverlay.expansionFrac
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		anchors.horizontalCenter: parent.horizontalCenter
		width: 1024
	}

    Connections {
        target: centralOverlay
        onActivePageTagChanged: {
            let tag = centralOverlay.activePageTag
            if (!tag) {
                contentPane.clear()
                return
            }
            if (tag === centralOverlay.pageNews) {
                contentPane.replace(newsComponent)
            } else if (tag === centralOverlay.pageProfile) {
                contentPane.replace(profileComponent)
            } else if (tag === centralOverlay.pagePlayOnline) {
                contentPane.replace(playOnlineComponent)
            } else if (tag === centralOverlay.pageLocalGame) {
                contentPane.replace(localGameComponent)
            } else if (tag === centralOverlay.pageSettings) {
                contentPane.replace(settingsComponent)
            } else if (tag === centralOverlay.pageDemos) {
                contentPane.replace(demosComponent)
            } else if (tag === centralOverlay.pageHelp) {
                contentPane.replace(helpComponent)
            } else if (tag === centralOverlay.pageQuit) {
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
	    if (centralOverlay.handleKeyEvent(event)) {
            return
        }

	    if (event.key !== Qt.Key_Escape) {
	        return
	    }
	    wsw.returnFromMainMenu()
	    root.forceActiveFocus()
	    event.accepted = true
	}
}