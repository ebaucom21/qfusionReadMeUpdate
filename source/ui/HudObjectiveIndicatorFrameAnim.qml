import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

ParallelAnimation {
    property real minSide
    property real maxSide
    property real minOpacity
    property int step1Duration
    property int step2Duration

    loops: Animation.Infinite

    SequentialAnimation {
        PropertyAnimation {
            target: frameArea
            properties: "width,height"
            from: minSide
            to: maxSide
            duration: step1Duration
        }
        PropertyAnimation {
            target: frameArea
            properties: "width,height"
            from: maxSide
            to: minSide
            duration: step2Duration
        }
    }
    SequentialAnimation {
        PropertyAnimation {
            target: frameArea
            property: "baseOpacity"
            from: 1.0
            to: minOpacity
            duration: step1Duration
        }
        PropertyAnimation {
            target: frameArea
            property: "baseOpacity"
            from: minOpacity
            to: 1.0
            duration: step2Duration
        }
    }
}