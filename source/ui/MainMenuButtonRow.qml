import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
	id: root
	height: 40

    property bool highlighted: false
	property string text
	property bool leaningRight: false
	property real expansionFrac: 0.0
	property bool enableSlidingOvershoot: true

	signal clicked()

	function toggleExpandedState() {
		if (state == "centered") {
			state = leaningRight ? "pinnedToLeft" : "pinnedToRight"
		} else {
			state = "centered"
		}
	}

	readonly property var transformMatrix: UI.ui.makeSkewXMatrix(root.height, 20.0)

	readonly property color foregroundColor: Qt.lighter(Material.backgroundColor, 1.5)
	readonly property color trailDecayColor: UI.ui.colorWithAlpha(Material.backgroundColor, 0.0)
	readonly property color highlightedColor: Material.accentColor

	states: [
		State {
			name: "centered"
			AnchorChanges {
				target: contentRow
				anchors.horizontalCenter: root.horizontalCenter
				anchors.left: undefined
				anchors.right: undefined
			}
		},
		State {
			name: "pinnedToLeft"
			AnchorChanges {
				target: contentRow
				anchors.horizontalCenter: undefined
				anchors.left: root.left
				anchors.right: undefined
			}
		},
		State {
			name: "pinnedToRight"
			AnchorChanges {
				target: contentRow
				anchors.horizontalCenter: undefined
				anchors.left: undefined
				anchors.right: root.right
			}
		}
	]

	transitions: [
	    Transition {
	        to: "centered"
		    AnchorAnimation {
			    duration: 333
			    easing.type: Easing.OutBack
		    }
	    },
	    Transition {
	        from: "centered"
	        AnchorAnimation {
	            duration: 333
	            easing.type: enableSlidingOvershoot ? Easing.OutBack : Easing.OutCubic
	        }
	    }
	]

	state: "centered"

	Loader {
		active: !leaningRight
		anchors.right: contentRow.left
		anchors.rightMargin: 4
		sourceComponent: leftTrailComponent
	}

	Component {
		id: leftTrailComponent
		MainMenuButtonTrail {
			leftColor: highlighted || mouseArea.containsMouse ? highlightedColor : foregroundColor
			rightColor: root.trailDecayColor
			transformMatrix: root.transformMatrix
		}
	}

    // The trail does not have a shadow but this is sufficent
    Item {
        z: -1
        id: shadowCaster
        anchors.centerIn: contentRow
        opacity: 0.5
        Item {
            anchors.centerIn: parent
            width: contentRow.width
            height: contentRow.height
		    transform: Matrix4x4 { matrix: root.transformMatrix }
            layer.enabled: true
            layer.effect: ElevationEffect { elevation: 16 }
        }
    }

	Rectangle {
		id: contentRow
		height: 40
		width: mouseArea.containsMouse ? UI.ui.mainMenuButtonWidthDp + 12 : UI.ui.mainMenuButtonWidthDp
		radius: 3
		color: highlighted || mouseArea.containsMouse ? highlightedColor : foregroundColor
		Behavior on width { SmoothedAnimation { duration: 333 } }

		transform: Matrix4x4 { matrix: root.transformMatrix }

		onXChanged: {
			const halfContainerWidth = parent.width / 2
			const halfThisWidth = width / 2
			const slidingDistance = halfContainerWidth - halfThisWidth
			let frac = 0.0
			if (root.leaningRight) {
				frac = Math.abs(x - halfContainerWidth + halfThisWidth) / slidingDistance
			} else {
				frac = Math.abs(x - halfContainerWidth + halfThisWidth) / slidingDistance
			}
			root.expansionFrac = Math.min(1.0, Math.max(frac, 0.0))
		}

		Label {
			anchors.left: root.leaningRight ? parent.left: undefined
			anchors.right: root.leaningRight ? undefined: parent.right
			anchors.verticalCenter: parent.verticalCenter
			anchors.leftMargin: 12
			anchors.rightMargin: 12
			font.family: UI.ui.headingFontFamily
			font.pointSize: 14
			font.letterSpacing: mouseArea.containsMouse ? 1.75 : 1.25
			text: root.text
			font.weight: Font.ExtraBold
			font.capitalization: Font.AllUppercase
			Behavior on font.letterSpacing { SmoothedAnimation { duration: 333 } }
		}

		MouseArea {
			id: mouseArea
			anchors.fill: parent
			hoverEnabled: true
			onClicked: {
			    let frac = root.expansionFrac
			    // Suppress clicked() signal in an immediate state
			    if (Math.abs(frac) < 0.001 || Math.abs(frac - 1.0) < 0.001) {
			        root.clicked()
			    }
			}
		}
	}

	Loader {
		active: leaningRight
		anchors.left: contentRow.right
		anchors.leftMargin: 4
		sourceComponent: rightTrailComponent
	}

	Component {
		id: rightTrailComponent
		MainMenuButtonTrail {
			leftColor: root.trailDecayColor
			rightColor: highlighted || mouseArea.containsMouse ? highlightedColor : foregroundColor
			transformMatrix: root.transformMatrix
		}
	}
}
