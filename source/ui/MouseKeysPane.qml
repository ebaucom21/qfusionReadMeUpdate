import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Rectangle {
    signal bindingRequested(int quakeKey)
    signal unbindingRequested(int quakeKey)
    signal keySelected(int quakeKey)

    property bool isInEditorMode

    color: Qt.rgba(0, 0, 0, 0.2)
    radius: 6
}