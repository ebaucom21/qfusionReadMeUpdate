import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

SequentialAnimation {
    id: root

    property var target
    property string targetProperty
    property real amplitude
    property int period: 500

    loops: Animation.Infinite

    NumberAnimation {
        target: root.target
        property: targetProperty
        duration: 0.25 * period
        from: 0
        to: -amplitude
    }
    NumberAnimation {
        target: root.target
        property: targetProperty
        duration: 0.5 * period
        from: -amplitude
        to: +amplitude
    }
    NumberAnimation {
        target: root.target
        property: targetProperty
        duration: 0.25 * period
        from: +amplitude
        to: 0
    }
}