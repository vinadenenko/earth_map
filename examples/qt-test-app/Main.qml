import QtQuick

// EarthMapQuickItem needs no import: it's a QML_ELEMENT registered in this
// same module (URI qt-test-app, from qt_add_qml_module in CMakeLists.txt),
// and Main.qml is part of that module too.
Window {
    width: 1280
    height: 720
    visible: true
    title: qsTr("Earth Map - Qt Test App")

    EarthMapQuickItem {
        anchors.fill: parent
        focus: true
    }
}
