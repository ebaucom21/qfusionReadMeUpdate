import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

ComboBox {
    id: root

    property bool autoFit: true
    property real minimumWidth
    property real maximumHeight: rootItem.height / 3

    // https://stackoverflow.com/a/57726740
    popup.contentItem.implicitHeight: Math.min(maximumHeight, root.popup.contentItem.contentHeight)

    TextMetrics {
        id: textMetrics
        font: root.font
    }

    onModelChanged: {
        if (autoFit) {
            let desiredWidth = minimumWidth
            // https://stackoverflow.com/a/45049993
            for (let i = 0; i < model.length; i++){
                textMetrics.text = model[i]
                desiredWidth = Math.max(textMetrics.width, desiredWidth)
            }
            if (desiredWidth) {
                implicitWidth = 56 + desiredWidth
            }
        }
    }
}