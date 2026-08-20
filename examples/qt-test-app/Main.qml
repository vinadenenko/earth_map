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
    property int maxValue: 1
    Connections {
        target: map
        function onLastFrameCpuMsChanged() {
            const value = map.lastFrameCpuMs
            cpuSeries.append(sample, value)
            sample++

            if (parseFloat(value) > maxValue) {
                maxValue = parseFloat(value)
            }

            if (cpuSeries.count > 300) {
                cpuSeries.remove(0)
            }

            cpuSeries.axisX.min = Math.max(0, sample - 300)
            cpuSeries.axisX.max = Math.max(300, sample)
            cpuSeries.axisY.max = Math.max(maxValue, value * 1.5)
        }
    }

    ChartView {
        width: 400
        height: 200
        anchors.right: parent.right
        Text {
            id: currentCpu
            anchors.right: parent.right
            anchors.top: parent.top

            text: "CPU: " + parseFloat(map.lastFrameCpuMs).toFixed(2) + " ms"

            font.pixelSize: 16
        }

        antialiasing: true
        SplineSeries {
            id: cpuSeries
            name: "CPU ms"
        }
    }
}
