import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    Item {
        id: field
        anchors.centerIn: parent
        width: 16 * 50
        height: 9 * 50
        clip: true

        Component.onCompleted: {
            hudEditorLayoutModel.setFieldSize(width, height)
            hudEditorLayoutModel.load("default")
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.horizontalCenter
            anchors.top: parent.top
            anchors.bottom: parent.verticalCenter
            color: Qt.rgba(1.0, 1.0, 1.0, 0.03)
        }
        Rectangle {
            anchors.left: parent.horizontalCenter
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.verticalCenter
            color: Qt.rgba(1.0, 1.0, 1.0, 0.05)
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.horizontalCenter
            anchors.top: parent.verticalCenter
            anchors.bottom: parent.bottom
            color: Qt.rgba(1.0, 1.0, 1.0, 0.05)
        }
        Rectangle {
            anchors.left: parent.horizontalCenter
            anchors.right: parent.right
            anchors.top: parent.verticalCenter
            anchors.bottom: parent.bottom
            color: Qt.rgba(1.0, 1.0, 1.0, 0.03)
        }

        HudEditorAnchorsMarker {
            displayedAnchors: hudEditorLayoutModel.displayedFieldAnchors
        }

        Repeater {
            id: repeater
            model: hudEditorLayoutModel
            delegate: HudLayoutItem {
                id: element
                width: size.width
                height: size.height
                onXChanged: handleCoordChanges()
                onYChanged: handleCoordChanges()
                state: "anchored"

                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(1.0, 0.0, 0.3, 0.3)
                }

                states: [
                    State {
                        name: "anchored"
                        when: !mouseArea.containsMouse
                        AnchorChanges {
                            target: element
                            anchors.top: getQmlAnchor(HudLayoutModel.Top)
                            anchors.bottom: getQmlAnchor(HudLayoutModel.Bottom)
                            anchors.left: getQmlAnchor(HudLayoutModel.Left)
                            anchors.right: getQmlAnchor(HudLayoutModel.Right)
                            anchors.horizontalCenter: getQmlAnchor(HudLayoutModel.HCenter)
                            anchors.verticalCenter: getQmlAnchor(HudLayoutModel.VCenter)
                        }
                    },
                    State {
                        name: "dragged"
                        when: mouseArea.containsMouse && mouseArea.drag.active
                        AnchorChanges {
                            target: element
                            anchors.top: undefined
                            anchors.bottom: undefined
                            anchors.left: undefined
                            anchors.right: undefined
                            anchors.horizontalCenter: undefined
                            anchors.verticalCenter: undefined
                        }
                        PropertyChanges {
                            target: element
                            x: origin.x
                            y: origin.y
                        }
                    },
                    State {
                        name: "detached"
                        when: mouseArea.containsMouse && !mouseArea.drag.active
                        AnchorChanges {
                            target: element
                            anchors.top: undefined
                            anchors.bottom: undefined
                            anchors.left: undefined
                            anchors.right: undefined
                            anchors.horizontalCenter: undefined
                            anchors.verticalCenter: undefined
                        }
                    }
                ]

                function getQmlAnchor(anchorBit) {
                    const anchorItem = anchorItemIndex >= 0 ? repeater.itemAt(anchorItemIndex) : field
                    return getQmlAnchorOfItem(selfAnchors, anchorItemAnchors, anchorBit, anchorItem)
                }

                function handleCoordChanges() {
                    if (mouseArea.drag.active) {
                        hudEditorLayoutModel.trackDragging(index, element.x, element.y)
                    } else {
                        hudEditorLayoutModel.updatePosition(index, element.x, element.y)
                    }
                }

                HudEditorAnchorsMarker {
                    displayedAnchors: model.displayedAnchors
                    highlighted: mouseArea.drag.active
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    drag.target: draggable ? parent : undefined
                    hoverEnabled: true
                    drag.onActiveChanged: {
                        if (!drag.active) {
                            hudEditorLayoutModel.finishDragging(index)
                        }
                    }
                    onContainsMouseChanged: {
                        if (!containsMouse) {
                            hudEditorLayoutModel.updateAnchors(index)
                        }
                    }
                }
            }
        }
    }
}

