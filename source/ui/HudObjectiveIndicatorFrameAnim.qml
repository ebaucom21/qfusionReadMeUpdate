import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

ParallelAnimation {
    id: root
    property real minSide
    property real maxSide
    property real minOpacity
    property int step1Duration
    property int step2Duration
    property var target

    loops: Animation.Infinite

    SequentialAnimation {
        PropertyAnimation {
            target: root.target
            properties: "width,height"
            from: minSide
            to: maxSide
            duration: step1Duration
        }
        PropertyAnimation {
            target: root.target
            properties: "width,height"
            from: maxSide
            to: minSide
            duration: step2Duration
        }
    }
    SequentialAnimation {
        PropertyAnimation {
            target: root.target
            property: "baseOpacity"
            from: 1.0
            to: minOpacity
            duration: step1Duration
        }
        PropertyAnimation {
            target: root.target
            property: "baseOpacity"
            from: minOpacity
            to: 1.0
            duration: step2Duration
        }
    }
}