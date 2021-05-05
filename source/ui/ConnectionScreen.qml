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
    }

    Component {
        id: logoComponent
        Item {
            Image {
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
                source: "logo.webp"
            }
        }
    }

    Component {
        id: dialogComponent
        Item {
            Rectangle {
                anchors.centerIn: parent
                width: 360
                height: 280
                color: Qt.darker(Material.background, 1.1)
                radius: 3
                layer.enabled: true
                layer.effect: ElevationEffect { elevation: 64 }

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 3
                    color: Material.accent
                }

                Label {
                    id: titleLabel
                    anchors.top: parent.top
                    anchors.topMargin: 20
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    horizontalAlignment: Qt.AlignHCenter
                    elide: Text.ElideRight
                    font.pointSize: 15
                    font.letterSpacing: 1.5
                    font.weight: Font.Normal
                    font.capitalization: Font.AllUppercase
                    // TODO: Supply a more appropriate title if needed
                    text: "Connection refused"
                }

                Label {
                    id: descLabel
                    anchors.top: titleLabel.bottom
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
                    text: wsw.connectionFailMessage
                }

                TextField {
                    id: passwordInput
                    visible: wsw.connectionFailKind === Wsw.PasswordRequired
                    enabled: visible
                    Material.theme: activeFocus ? Material.Light : Material.Dark
                    anchors.top: descLabel.bottom
                    anchors.topMargin: 20
                    anchors.horizontalCenter: parent.horizontalCenter
                    horizontalAlignment: Qt.AlignHCenter
                    width: 128
                    maximumLength: 16
                    echoMode: TextInput.Password
                    onEditingFinished: wsw.reconnectWithPassword(text)
                }

                Row {
                    id: buttonRow
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.bottomMargin: 2
                    anchors.rightMargin: 8
                    spacing: 8
                    Button {
                        text: retryButton.visible ? "Cancel" : "OK"
                        flat: true
                        highlighted: false
                        onClicked: wsw.clearFailedConnectionState()
                    }
                    Button {
                        id: retryButton
                        visible: wsw.connectionFailKind !== Wsw.DontReconnect
                        enabled: wsw.connectionFailKind === Wsw.TryReconnecting ||
                                 (wsw.connectionFailKind === Wsw.PasswordRequired && text.length)
                        text: "Retry"
                        flat: true
                        highlighted: true
                        onClicked: {
                            if (wsw.connectionFailKind === Wsw.PasswordRequired) {
                                wsw.reconnectWithPassword(text)
                            } else {
                                wsw.reconnect()
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: wsw
        onConnectionFailKindChanged: {
            if (wsw.connectionFailKind) {
                stackView.clear(StackView.Immediate)
                stackView.push(dialogComponent, null, StackView.ReplaceTransition)
            } else {
                stackView.clear(StackView.Immediate)
                stackView.push(logoComponent, null, StackView.ReplaceTransition)
            }
        }
    }
}