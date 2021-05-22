import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

AutoFittingComboBox {
    id: root

    Material.theme: Material.Dark
    Material.accent: "orange"

    property int oldWidth
    property int oldHeight

    property bool skipIndexChangeSignal: true

    readonly property var knownWidthValues: wsw.videoModeWidthValuesList
    readonly property var knownHeightValues: wsw.videoModeHeightValuesList

    property var headings: wsw.videoModeHeadingsList
    property var widthValues: knownWidthValues
    property var heightValues: knownHeightValues

    model: headings

    function parse(s) {
        const value = parseInt(s, 10)
        return value === value ? value : 0
    }

    function getWidth() { return parse(wsw.getCVarValue("vid_width")) }
    function getHeight() { return parse(wsw.getCVarValue("vid_height")) }

    function checkCVarChanges() {
        const width = getWidth()
        const height = getHeight()
        if (width !== oldWidth || height !== oldHeight) {
            if (!wsw.hasControlPendingCVarChanges(root)) {
                setNewValues(width, height)
            }
        }
    }

    function setNewValues(width, height) {
        let index = mapValuesToIndex(width, height)
        if (index < 0) {
            // Add a placeholder if needed. Otherwise just update the placeholder value
            if (headings[headings.length - 1] !== "- -") {
                headings.push("- -")
                widthValues.push(width)
                heightValues.push(height)
                model = headings
            } else {
                widthValues[widthValues.length - 1] = width
                heightValues[heightValues.length - 1] = height
            }
            index = widthValues.length - 1
        } else {
            // Remove the placeholder if it's no longer useful
            if (index != headings.length - 1 && headings[headings.length - 1] === "- -") {
                widthValues.pop()
                heightValues.pop()
                headings.pop()
                model = headings
            }
        }

        oldWidth = width
        oldHeight = height
        if (root.currentIndex != index) {
            root.currentIndex = index
        }
    }

    function mapValuesToIndex(width, height) {
        for (let i = 0; i < knownWidthValues.length; ++i) {
            if (knownWidthValues[i] === width && knownHeightValues[i] === height) {
                return i
            }
        }
        return -1
    }

    function rollbackChanges() {
        skipIndexChangeSignal = true
        setNewValues(getWidth(), getHeight())
        skipIndexChangeSignal = false
    }

    onCurrentIndexChanged: {
        if (!skipIndexChangeSignal) {
            const width = widthValues[currentIndex]
            const height = heightValues[currentIndex]
            wsw.markPendingCVarChanges(root, "vid_width", width, true)
            wsw.markPendingCVarChanges(root, "vid_height", height, true)
        }
    }

    Component.onCompleted: {
        console.assert(heightValues.length === widthValues.length)
        console.assert(headings.length === widthValues.length)
        setNewValues(getWidth(), getHeight())
        wsw.registerCVarAwareControl(root)
        skipIndexChangeSignal = false
    }

    Component.onDestruction: wsw.unregisterCVarAwareControl(root)
}