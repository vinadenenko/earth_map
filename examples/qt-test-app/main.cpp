#include <QApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

int main(int argc, char *argv[])
{
#ifdef Q_OS_LINUX
    // GLEW's Linux build resolves extensions through GLX internally
    // (glewInit() calls into glxewInit()). On a native Wayland session Qt's
    // "wayland" platform plugin creates contexts via EGL only -- there is
    // no GLX context for GLEW to find, so glewInit() fails unconditionally
    // regardless of the GL context itself being fine. Forcing the xcb
    // platform plugin (via XWayland, which is present whenever DISPLAY is
    // set) with GLX integration gives GLEW a real GLX context. Must be set
    // before QGuiApplication loads the platform plugin.
    // qputenv("QT_QPA_PLATFORM", "xcb");
    // qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
#endif

    // EarthMapQuickItem draws straight into the window via raw GL calls
    // interleaved into Qt Quick's own command stream (the "OpenGL Under
    // QML" pattern, not QQuickFramebufferObject), which only works when
    // the scene graph runs on the OpenGL backend.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("EarthMapExample", "Main");

    return app.exec();
}
