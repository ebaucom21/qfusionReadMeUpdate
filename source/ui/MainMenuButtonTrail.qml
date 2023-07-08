import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Row {
	id: root
	spacing: 4

	property var transformMatrix
	property color leftColor
	property color rightColor
	readonly property real elementWidth: 20
	readonly property real elementsCount: Math.floor(UI.ui.mainMenuButtonTrailWidthDp / (elementWidth + root.spacing))

	Repeater {
		model: elementsCount
		Rectangle {
			property real frac: index / elementsCount
			// TODO: Must use gamma-correct lerp
			color: Qt.rgba(leftColor.r * frac + rightColor.r * (1.0 - frac),
						   leftColor.g * frac + rightColor.g * (1.0 - frac),
						   leftColor.b * frac + rightColor.b * (1.0 - frac),
						   leftColor.a * frac + rightColor.a * (1.0 - frac));
			width: elementWidth
			height: 40
			radius: 3

			transform: Matrix4x4 {
				matrix: root.transformMatrix
			}
		}
	}
}