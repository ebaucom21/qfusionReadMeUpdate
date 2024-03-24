pragma Singleton
import QtQuick 2.12
import net.warsow 2.6

QtObject {
    readonly property var ui: __ui
    readonly property var serverListModel: __serverListModel
    readonly property var hudCommonDataModel: __hudCommonDataModel
    readonly property var hudPovDataModel: __hudPovDataModel
    readonly property var regularHudEditorModel: __regularHudEditorModel
    readonly property var miniviewHudEditorModel: __miniviewHudEditorModel
    readonly property var keysAndBindings: __keysAndBindings
    readonly property var demosModel: __demosModel
    readonly property var demosResolver: __demosResolver
    readonly property var demoPlayer: __demoPlayer
    readonly property var gametypesModel: __gametypesModel
    readonly property var gametypeOptionsModel: __gametypeOptionsModel
    readonly property var regularCallvotesModel: __regularCallvotesModel
    readonly property var operatorCallvotesModel: __operatorCallvotesModel
    readonly property var chatProxy: __chatProxy
    readonly property var teamChatProxy: __teamChatProxy
    readonly property var playersModel: __playersModel
    readonly property var scoreboard: __scoreboard
    readonly property var scoreboardSpecsModel: __scoreboardSpecsModel
    readonly property var scoreboardPlayersModel: __scoreboardPlayersModel
    readonly property var scoreboardAlphaModel: __scoreboardAlphaModel
    readonly property var scoreboardBetaModel: __scoreboardBetaModel
    readonly property var scoreboardMixedModel: __scoreboardMixedModel

    function destroyLayer(layer) {
        if (layer) {
            layer.enabled = false
            layer.effect  = null
            ui.ensureObjectDestruction(layer)
        }
    }
}