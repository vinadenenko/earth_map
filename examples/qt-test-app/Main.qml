import QtQuick
import QtQuick.Controls
import QtCharts

import EarthMapExample

Window {
    width: 1280
    height: 720
    visible: true
    title: qsTr("Earth Map - Qt Test App")

    function bytesToMiB(bytes) {
        return bytes / (1024 * 1024)
    }

    EarthMapQuickItem {
        id: map
        anchors.fill: parent
        // anchors.margins: 100
        focus: true
    }

    property int sample: 0

    function seriesMaximum(series) {
        let maximum = 0
        for (let index = 0; index < series.count; ++index) {
            const value = series.at(index).y
            if (Number.isFinite(value)) {
                maximum = Math.max(maximum, value)
            }
        }
        return maximum
    }

    function rescaleChartYAxis() {
        // The chart keeps only its latest samples. Recompute from that same
        // window so an old upload stall does not leave normal frame times
        // compressed against the x axis forever.
        const visibleMaximum = Math.max(seriesMaximum(cpuSeries), seriesMaximum(gpuSeries))
        cpuSeries.axisY.min = 0
        cpuSeries.axisY.max = Math.max(1, visibleMaximum * 1.15)
    }

    Connections {
        target: map
        function onPerformanceStatsChanged() {
            const cpuValue = map.frameCpuMs
            cpuSeries.append(sample, cpuValue)
            if (map.hasFrameGpuMs) {
                gpuSeries.append(sample, map.frameGpuMs)
            }
            sample++

            if (cpuSeries.count > 100) {
                cpuSeries.remove(0)
            }
            if (gpuSeries.count > 100) {
                gpuSeries.remove(0)
            }

            cpuSeries.axisX.min = Math.max(0, sample - 100)
            cpuSeries.axisX.max = Math.max(100, sample)
            rescaleChartYAxis()
        }
    }

    // This is intentionally a QML-only layer. EarthMapQuickItem submits its
    // OpenGL globe as an underlay; all application controls and diagnostics
    // belong together above it in the Qt Quick scene graph.
    Item {
        id: hud
        anchors.fill: parent
        z: 99

        ChartView {
        id: chart
        width: parent.width
        height: 200
        anchors.bottom: parent.bottom

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

    // Mirrors earth_map::TileRenderStats (see EarthMapQuickItem.h) --
    // composition/residency data, kept as its own panel since the library
    // itself keeps this separate from PerformanceStats above.



        Rectangle {
        width: 280
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        color: "#d0202020"
        radius: 6
        border.color: "#808080"
        border.width: 1
        implicitHeight: performanceStats.implicitHeight + 16

        Column {
            id: performanceStats
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Text {
                text: "FPS: " + map.fps
                color: "white"
                font.pixelSize: 16
            }
            Text {
                text: "CPU: " + map.frameCpuMs.toFixed(2) + " ms"
                color: "white"
                font.pixelSize: 16
            }
            Text {
                text: "GPU: " + (map.hasFrameGpuMs ? map.frameGpuMs.toFixed(2) + " ms" : "n/a")
                color: "white"
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
                    color: "white"
                    font.pixelSize: 13
                }
            }

            Text {
                text: "Visible tiles: " + map.visibleTiles
                color: "white"
                font.pixelSize: 16
            }
            Text {
                text: "Rendered tiles: " + map.renderedTiles
                color: "white"
                font.pixelSize: 13
            }
            Text {
                text: "Average zoom: " + map.averageLod.toFixed(2)
                color: "white"
                font.pixelSize: 13
            }

            Text {
                text: "Tile pool layers: " + map.occupiedPoolLayers + " / " + map.maxPoolLayers
                color: "white"
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
                color: "white"
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
                color: "white"
                font.pixelSize: 13
            }
        }
    }

        Rectangle {
        width: 290
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        color: "#d0202020"
        radius: 6
        border.color: "#808080"
        border.width: 1
        implicitHeight: scenarioControls.implicitHeight + 16

        Column {
            id: scenarioControls
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            Text {
                text: "Camera and performance scenarios"
                color: "white"
                font.bold: true
                font.pixelSize: 14
            }

            Row {
                spacing: 6

                Button {
                    text: "Run steady z13"
                    enabled: !map.performanceScenarioActive
                    onClicked: map.startPerformanceScenario("steady-z13")
                }
                Button {
                    text: "Run flight"
                    enabled: !map.performanceScenarioActive
                    onClicked: map.startPerformanceScenario("flight")
                }
            }

            Button {
                text: "Preview flight"
                enabled: !map.performanceScenarioActive
                onClicked: map.startPerformanceScenario("flight-preview")
            }

            Button {
                text: "Stop scenario"
                enabled: map.performanceScenarioActive
                onClicked: map.stopPerformanceScenario()
            }

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                color: map.performanceScenarioActive ? "#ffcc66" : "white"
                text: map.performanceScenarioStatus.length > 0
                      ? map.performanceScenarioStatus
                      : "Benchmarks warm up and write JSON; preview keeps the HUD live."
                font.pixelSize: 12
            }

            Text {
                visible: map.performanceScenarioReportPath.length > 0
                width: parent.width
                wrapMode: Text.WrapAnywhere
                color: "#a8e6a3"
                text: map.performanceScenarioReportPath
                font.pixelSize: 11
            }
        }
    }
    }
}
