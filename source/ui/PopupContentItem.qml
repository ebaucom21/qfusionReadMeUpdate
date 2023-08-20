import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    id: root

    implicitWidth: UI.ui.desiredPopupWidth
    implicitHeight: UI.ui.desiredPopupHeight
    focus: true

    default property Component contentComponent

    readonly property alias contentItem: loader.item

    property string title
    property bool active
    property string acceptButtonText: "OK"
    property string rejectButtonText: "Cancel"

    property bool hasRejectButton: true
    property bool rejectButtonEnabled: true
    property bool hasAcceptButton: false
    property bool acceptButtonEnabled: true

    // Allows a fine tuning while using not within real popups
    property real buttonsRowBottomMargin: -8
    property real buttonsRowRightMargin: 0

    signal accepted()
    signal rejected()
    signal dismissed()

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
        maximumLineCount: 2
        font.family: UI.ui.headingFontFamily
        font.pointSize: 14
        font.letterSpacing: 1.25
        font.weight: Font.Medium
        font.capitalization: Font.AllUppercase
        text: root.title
    }

    Loader {
        id: loader
        active: root.active
        anchors.top: titleLabel.bottom
        anchors.bottom: buttonsRow.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        sourceComponent: contentComponent
    }

    Item {
        id: buttonsRow
        anchors.bottom: parent.bottom
        anchors.bottomMargin: buttonsRowBottomMargin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: buttonsRowRightMargin
        height: Math.max(rejectButton.height, acceptButton.height)

        Button {
            id: rejectButton
            anchors.bottom: parent.bottom
            anchors.right: acceptButton.left
            anchors.rightMargin: 12
            width: UI.ui.popupButtonWidth
            visible: hasRejectButton
            enabled: rejectButtonEnabled
            flat: true
            text: rejectButtonText
            onClicked: root.rejected()
            states: [
                State {
                    name: "rightmost"
                    when: !hasAcceptButton
                    AnchorChanges {
                        target: rejectButton
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                    }
                },
                State {
                    name: "leftmost"
                    when: hasAcceptButton
                    AnchorChanges {
                        target: rejectButton
                        anchors.bottom: parent.bottom
                        anchors.right: acceptButton.left
                    }
                }
            ]
            transitions: Transition { AnchorAnimation { duration: 33 } }
        }
        Button {
            id: acceptButton
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: UI.ui.popupButtonWidth
            visible: hasAcceptButton
            enabled: acceptButtonEnabled
            highlighted: acceptButtonEnabled
            text: acceptButtonText
            onClicked: root.accepted()
        }
    }

    Keys.onPressed: {
        if (event.key === Qt.Key_Escape) {
            event.accepted = true
            root.dismissed()
        }
    }
}