import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

Row {
	id: root
	spacing: 4

	property var transformMatrix
	property color leftColor
	property color rightColor

	Repeater {
		model: 10
		Rectangle {
			property real frac: index / 10.0
			color: Qt.rgba(leftColor.r * frac + rightColor.r * (1.0 - frac),
						   leftColor.g * frac + rightColor.g * (1.0 - frac),
						   leftColor.b * frac + rightColor.b * (1.0 - frac),
						   leftColor.a * frac + rightColor.a * (1.0 - frac));
			width: 20
			height: 40
			radius: 3

			transform: Matrix4x4 {
				matrix: root.transformMatrix
			}
		}
	}
}