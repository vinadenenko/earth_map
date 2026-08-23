import QtQuick
import QtQuick.Controls
import QtCharts

import EarthMapExample

Window {
    width: 1280
    height: 720
    visible: true
    title: qsTr("Earth Map - Qt Test App")

    EarthMapQuickItem {
        id: map
        anchors.fill: parent
        focus: true
    }

    property int sample: 0
    property real maxValue: 1
    Connections {
        target: map
        function onPerformanceStatsChanged() {
            const cpuValue = map.frameCpuMs
            cpuSeries.append(sample, cpuValue)
            if (map.hasFrameGpuMs) {
                gpuSeries.append(sample, map.frameGpuMs)
            }
            sample++

            let maxSeen = cpuValue
            if (map.hasFrameGpuMs && map.frameGpuMs > maxSeen) {
                maxSeen = map.frameGpuMs
            }
            if (maxSeen > maxValue) {
                maxValue = maxSeen
            }

            if (cpuSeries.count > 100) {
                cpuSeries.remove(0)
            }
            if (gpuSeries.count > 100) {
                gpuSeries.remove(0)
            }

            cpuSeries.axisX.min = Math.max(0, sample - 100)
            cpuSeries.axisX.max = Math.max(100, sample)
            cpuSeries.axisY.max = maxValue * 1.5
        }
    }

    ChartView {
        id: chart
        width: 400
        height: 200
        anchors.right: parent.right

        antialiasing: true
        legend.visible: true

        SplineSeries {
            id: cpuSeries
            name: "CPU ms"
        }
        SplineSeries {
            id: gpuSeries
            name: "GPU ms"
        }
    }

    Column {
        anchors.right: parent.right
        anchors.top: chart.bottom
        anchors.margins: 8
        spacing: 4

        Text {
            text: "FPS: " + map.fps
            font.pixelSize: 16
        }
        Text {
            text: "CPU: " + map.frameCpuMs.toFixed(2) + " ms"
            font.pixelSize: 16
        }
        Text {
            text: "GPU: " + (map.hasFrameGpuMs ? map.frameGpuMs.toFixed(2) + " ms" : "n/a")
            font.pixelSize: 16
        }

        // TEMPORARY, investigation only -- naive app-side measurement,
        // independent of earth_map's own PerformanceStats above. Cross-check
        // the two while mangohud is unavailable; remove once cross-checked.
        Text {
            text: "APP FPS: " + map.appFps
            font.pixelSize: 16
            color: "orange"
        }
        Text {
            text: "APP CPU: " + map.appCpuMs.toFixed(2) + " ms"
            font.pixelSize: 16
            color: "orange"
        }

        Repeater {
            model: map.zoneTimings
            delegate: Text {
                required property var modelData
                text: "  " + modelData.name + ": "
                      + modelData.cpuMs.toFixed(2) + " ms cpu"
                      + (modelData.hasGpuMs ? " / " + modelData.gpuMs.toFixed(2) + " ms gpu" : "")
                      + (modelData.drawCalls > 0 ? " (" + modelData.drawCalls + " draws)" : "")
                font.pixelSize: 13
            }
        }
    }

    // Mirrors earth_map::TileRenderStats (see EarthMapQuickItem.h) --
    // composition/residency data, kept as its own panel since the library
    // itself keeps this separate from PerformanceStats above.
    function bytesToMiB(bytes) {
        return bytes / (1024 * 1024)
    }

    Column {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 4
        width: 260

        Text {
            text: "Visible tiles: " + map.visibleTiles
            font.pixelSize: 16
        }
        Text {
            text: "Rendered tiles: " + map.renderedTiles
            font.pixelSize: 13
        }
        Text {
            text: "Loaded this frame: " + map.tilesLoadedThisFrame
            font.pixelSize: 13
        }
        Text {
            text: "Average zoom: " + map.averageLod.toFixed(2)
            font.pixelSize: 13
        }

        Text {
            text: "Tile pool layers: " + map.occupiedPoolLayers + " / " + map.maxPoolLayers
            font.pixelSize: 13
        }
        ProgressBar {
            width: parent.width
            from: 0
            to: Math.max(1, map.maxPoolLayers)
            value: map.occupiedPoolLayers
        }

        Text {
            text: "Tile pool VRAM: " + bytesToMiB(map.tilePoolBytesUsed).toFixed(1) + " / "
                  + bytesToMiB(map.tilePoolBytesMax).toFixed(1) + " MiB"
            font.pixelSize: 13
        }
        ProgressBar {
            width: parent.width
            from: 0
            to: Math.max(1, map.tilePoolBytesMax)
            value: map.tilePoolBytesUsed
        }

        Text {
            text: "Indirection VRAM: " + bytesToMiB(map.indirectionBytesUsed).toFixed(2) + " MiB"
            font.pixelSize: 13
        }
    }
}
