#pragma once

#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlEngine>

#include <vector>

#include <earth_map/renderer/camera.h>  // earth_map::InputEvent

// Forward-declared so EarthMapQuickItem can grant it friendship below.
// Defined in EarthMapQuickItem.cpp.
namespace earth_map_qt_detail {
class EarthMapRenderer;
}

/**
 * @brief QML item that renders the earth_map globe directly into the Qt
 * Quick window's own render target -- no intermediate FBO/texture-blit.
 *
 * earth_map owns no window or GL context of its own (see
 * examples/basic_example.cpp: the consumer creates the context and drives
 * Initialize()/Render()/Resize() directly). This follows the pattern Qt
 * ships as "Scene Graph - OpenGL Under QML"
 * (QQuickWindow::beforeRendering()/beforeRenderPassRecording()), rather
 * than QQuickFramebufferObject: that pattern draws straight into the
 * window instead of compositing a separately-rendered texture, which is
 * both cheaper and avoids the FBO-vs-scenegraph texture-origin mismatch
 * that made the globe render upside down under QQuickFramebufferObject.
 *
 * Unlike Qt's own Squircle example -- which always covers the whole
 * window -- this item scopes its GL viewport and scissor rect to its own
 * QML geometry (position + size, in EarthMapRenderer::paint()), so it
 * behaves like any ordinary Item: positioned, sized, and clipped by QML,
 * safe to place alongside sibling items.
 *
 * Mouse/keyboard events arrive here on the GUI thread, but earth_map::
 * CameraController is not thread-safe and must only be touched from the
 * render thread where EarthMapRenderer::paint() runs. Events are queued
 * here and handed off in EarthMapQuickItem::sync(), which -- like the
 * Squircle example -- runs on QQuickWindow::beforeSynchronizing(), the one
 * point Qt Quick guarantees both threads are quiesced.
 */
class EarthMapQuickItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit EarthMapQuickItem(QQuickItem* parent = nullptr);

public slots:
    void sync();
    void cleanup();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private slots:
    void handleWindowChanged(QQuickWindow* window);

private:
    friend class earth_map_qt_detail::EarthMapRenderer;

    void releaseResources() override;

    void QueueEvent(const earth_map::InputEvent& event);

    // Only sync() may call this -- see the thread-safety note above.
    std::vector<earth_map::InputEvent> TakePendingEvents();

    // This item's geometry mapped into the window's device-pixel, GL
    // bottom-left-origin space -- what EarthMapRenderer needs for
    // glViewport()/glScissor(). Computed in sync() (GUI thread, where
    // mapToScene() and devicePixelRatio() are safe to call).
    QRect DeviceViewportRect() const;

    std::vector<earth_map::InputEvent> pending_events_;
    earth_map_qt_detail::EarthMapRenderer* renderer_ = nullptr;
};
