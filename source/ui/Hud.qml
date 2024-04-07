pragma Singleton
import QtQuick 2.12
import net.warsow 2.6

QtObject {
    readonly property var ui: __ui
    readonly property var commonDataModel: __hudCommonDataModel
    readonly property var povDataModel: __hudPovDataModel
    readonly property var actionRequestsModel: __actionRequestsModel
    readonly property var chatProxy: __chatProxy
    readonly property var teamChatProxy: __teamChatProxy

    readonly property var elementMargin: 16
    readonly property var elementRadius: 6
    readonly property var elementElevation: 16
    readonly property var elementBackgroundColor: Qt.rgba(0.0, 0.0, 0.0, 0.7)
    readonly property var miniviewItemWidth: 240
    readonly property var miniviewItemHeight: 160
    readonly property var miniviewBorderWidth: 4
    readonly property var teamScoreHeight: 80
    readonly property var valueBarWidth: 256 + 64 + 16

    function destroyLayer(layer) {
        if (layer) {
            layer.enabled = false
            layer.effect  = null
            ui.ensureObjectDestruction(layer)
        }
    }
}