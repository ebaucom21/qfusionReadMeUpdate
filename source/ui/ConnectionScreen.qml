import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
    id: root

    RadialGradient {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.darker(Material.background, 1.1) }
            GradientStop { position: 0.9; color: Qt.darker(Material.background, 1.9) }
        }
    }

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: logoComponent
        // Try making the dialog behaving like a regular popup
        replaceEnter: Transition {
            NumberAnimation {
                property: "scale"
                from: 0.0; to: 1.0
                duration: 200
            }
            NumberAnimation {
                property: "opacity"
                from: 0.0; to: 1.0
                duration: 144
            }
        }
    }

    Component {
        id: logoComponent
        Item {
            Image {
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
			    source: "image://wsw/gfx/ui/loadinglogo"
            }
        }
    }

    Component {
        id: dialogComponent
        Item {
            property real scale: 0.0
            PopupBackground {
                id: popupLikeBackground
                width: implicitWidth * scale
                height: implicitHeight * scale
                anchors.centerIn: parent
            }
            PopupContentItem {
                width: popupLikeBackground.width
                height: popupLikeBackground.height
                title: UI.ui.droppedConnectionTitle
                active: true
                anchors.centerIn: parent
                buttonsRowBottomMargin: +4
                buttonsRowRightMargin: +8
                acceptButtonText: "Retry"
                hasAcceptButton: UI.ui.reconnectBehaviour !== UISystem.DontReconnect
                acceptButtonEnabled: UI.ui.reconnectBehaviour === UISystem.OfUserChoice ||
                                     (UI.ui.reconnectBehaviour === UISystem.RequestPassword && contentItem.passwordText.length > 0)
                rejectButtonText: hasAcceptButton ? "Cancel" : "OK"
                onAccepted: {
                    if (UI.ui.reconnectBehaviour === UISystem.RequestPassword) {
                        UI.ui.reconnectWithPassword(contentItem.passwordText)
                    } else {
                        UI.ui.reconnect()
                    }
                }
                onRejected: UI.ui.stopReactingToDroppedConnection()
                onDismissed: UI.ui.stopReactingToDroppedConnection()
                contentComponent: Item {
                    property alias passwordText: passwordInput.text
                    Label {
                        id: descLabel
                        anchors.top: parent.top
                        anchors.topMargin: 20
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        wrapMode: Text.WordWrap
                        maximumLineCount: 4
                        horizontalAlignment: text.length > 50 ? Qt.AlignLeft : Qt.AlignHCenter
                        elide: Qt.ElideRight
                        lineHeight: 1.25
                        font.pointSize: 12
                        font.letterSpacing: 0.5
                        text: UI.ui.droppedConnectionMessage
                    }

                    TextField {
                        id: passwordInput
                        visible: UI.ui.reconnectBehaviour === UISystem.RequestPassword
                        enabled: visible
                        Material.theme: activeFocus ? Material.Light : Material.Dark
                        anchors.top: descLabel.bottom
                        anchors.topMargin: 20
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Qt.AlignHCenter
                        width: 128
                        maximumLength: 16
                        echoMode: TextInput.Password
                        onEditingFinished: UI.ui.reconnectWithPassword(passwordInput.text)
                    }
                }
            }
        }
    }

    Connections {
        target: UI.ui
        onIsReactingToDroppedConnectionChanged: {
            if (UI.ui.isReactingToDroppedConnection) {
                stackView.clear(StackView.Immediate)
                stackView.push(dialogComponent, null, StackView.ReplaceTransition)
            } else {
                stackView.clear(StackView.Immediate)
                stackView.push(logoComponent, null, StackView.Immediate)
            }
        }
    }

    Keys.onPressed: {
        if (event.key === Qt.Key_Escape) {
            UI.ui.stopReactingToDroppedConnection()
            UI.ui.disconnect()
        }
    }
}