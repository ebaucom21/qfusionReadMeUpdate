import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtMultimedia 5.12
import net.warsow 2.6

Item {
    id: root
    property string filePath
    property var altImagePath
    property color overlayColor: Material.background
    property real videoOpacity: 0.5
    property real overlayOpacity: 0.1

    WswVideoSource {
        id: videoSource
        filePath: root.filePath
    }

    VideoOutput {
        anchors.fill: parent
        source: videoSource
        opacity: videoOpacity
    }

    Loader {
        anchors.fill: parent
        active: (videoSource.status === WswVideoSource.Error) && !!altImagePath
        sourceComponent: Image {
            width: root.width
            height: root.height
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            source: altImagePath
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: overlayOpacity > 0.0
        color: overlayColor
        opacity: overlayOpacity
    }
}