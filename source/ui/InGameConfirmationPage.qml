import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    property alias titleText: titleLabel.text
    property alias actionText: acceptButton.text

    signal accepted()
    signal rejected()

    ColumnLayout {
        id: primaryColumn
        anchors.centerIn: parent
        width: parent.width - 192
        spacing: 48

        Label {
            id: titleLabel
            Layout.alignment: Qt.AlignHCenter
            font.pointSize: 18
            font.letterSpacing: 1.25
            font.capitalization: Font.SmallCaps
            font.weight: Font.Medium
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 32

            InGameButton {
                id: acceptButton
                Layout.preferredWidth: primaryColumn.width / 2
                text: "Proceed"
                displayIconPlaceholder: false
                onClicked: root.accepted()
            }

            InGameButton {
                Layout.preferredWidth: primaryColumn.width / 2
                highlighted: true
                text: "Go back"
                displayIconPlaceholder: false
                onClicked: root.rejected()
            }
        }
    }
}