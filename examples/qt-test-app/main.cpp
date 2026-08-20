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

    // EarthMapItem embeds earth_map via QQuickFramebufferObject, which only
    // composites when the Qt Quick scene graph runs on the OpenGL backend
    // (earth_map itself issues raw GL calls loaded by GLEW). Must be set
    // before QGuiApplication is constructed.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("qt-test-app", "Main");

    return app.exec();
}
