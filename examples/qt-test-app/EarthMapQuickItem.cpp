// GLEW must be included before anything that might pull in the system GL
// headers (Qt's OpenGL-adjacent headers below do) -- same ordering
// requirement as examples/basic_example.cpp. Not applicable on Android:
// GLES entry points are directly linked, no loader needed.
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

#include "EarthMapQuickItem.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRunnable>
#include <QWheelEvent>

#include <earth_map/core/camera_controller.h>
#include <earth_map/earth_map.h>

#include <algorithm>
#include <utility>

namespace {

// earth_map::CameraController (src/renderer/camera.cpp) hardcodes GLFW's
// arrow-key codes, which differ from Qt::Key_Up/Down/Left/Right. Letter
// keys need no translation: Qt::Key_A..Z already equal their ASCII
// uppercase codes, same as GLFW's.
constexpr int kGlfwKeyUp = 265;
constexpr int kGlfwKeyDown = 264;
constexpr int kGlfwKeyLeft = 263;
constexpr int kGlfwKeyRight = 262;

int ToEarthMapKeyCode(int qt_key) {
    switch (qt_key) {
        case Qt::Key_Up:
            return kGlfwKeyUp;
        case Qt::Key_Down:
            return kGlfwKeyDown;
        case Qt::Key_Left:
            return kGlfwKeyLeft;
        case Qt::Key_Right:
            return kGlfwKeyRight;
        default:
            return qt_key;
    }
}

// earth_map::CameraController checks event.button against GLFW's numbering
// (0 = left, 2 = middle), not Qt::MouseButton's bit flags.
int ToEarthMapButton(Qt::MouseButton button) {
    switch (button) {
        case Qt::LeftButton:
            return 0;
        case Qt::RightButton:
            return 1;
        case Qt::MiddleButton:
            return 2;
        default:
            return -1;
    }
}

// One notch of a standard mouse wheel is 120 in Qt's eighths-of-a-degree
// units. earth_map's zoom step (src/renderer/camera.cpp,
// HandleMouseScroll) expects ~1.0 per notch, matching GLFW's yoffset.
constexpr float kQtWheelUnitsPerNotch = 120.0f;

uint64_t NowMillis() {
    return static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
}

}  // namespace

EarthMapQuickItem::EarthMapQuickItem(QQuickItem* parent) : QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setFlag(QQuickItem::ItemIsFocusScope, true);
    connect(this, &QQuickItem::windowChanged, this, &EarthMapQuickItem::handleWindowChanged);
}

QRect EarthMapQuickItem::DeviceViewportRect() const {
    if (!window()) {
        return QRect();
    }

    const qreal dpr = window()->devicePixelRatio();
    const QPointF top_left_in_window = mapToScene(QPointF(0, 0));

    const qreal x_px = top_left_in_window.x() * dpr;
    const qreal w_px = width() * dpr;
    const qreal h_px = height() * dpr;

    // Qt's coordinate system has y=0 at the top; OpenGL's viewport/scissor
    // origin is bottom-left. Flip using the window's device-pixel height
    // (not this item's own height), since the result must land in the
    // window framebuffer's coordinate space.
    const qreal top_y_px = top_left_in_window.y() * dpr;
    const qreal window_height_px = window()->height() * dpr;
    const qreal y_px = window_height_px - top_y_px - h_px;

    return QRect(qRound(x_px), qRound(y_px), qRound(w_px), qRound(h_px));
}

void EarthMapQuickItem::QueueEvent(const earth_map::InputEvent& event) {
    pending_events_.push_back(event);
}

std::vector<earth_map::InputEvent> EarthMapQuickItem::TakePendingEvents() {
    return std::exchange(pending_events_, {});
}

void EarthMapQuickItem::mousePressEvent(QMouseEvent* event) {
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::MOUSE_BUTTON_PRESS;
    input_event.button = ToEarthMapButton(event->button());
    input_event.x = static_cast<float>(event->position().x());
    input_event.y = static_cast<float>(event->position().y());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::mouseReleaseEvent(QMouseEvent* event) {
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::MOUSE_BUTTON_RELEASE;
    input_event.button = ToEarthMapButton(event->button());
    input_event.x = static_cast<float>(event->position().x());
    input_event.y = static_cast<float>(event->position().y());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::mouseMoveEvent(QMouseEvent* event) {
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::MOUSE_MOVE;
    input_event.x = static_cast<float>(event->position().x());
    input_event.y = static_cast<float>(event->position().y());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::hoverMoveEvent(QHoverEvent* event) {
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::MOUSE_MOVE;
    input_event.x = static_cast<float>(event->position().x());
    input_event.y = static_cast<float>(event->position().y());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::mouseDoubleClickEvent(QMouseEvent* event) {
    // Qt detects the double-click itself (OS double-click interval), unlike
    // basic_example.cpp which tracks its own click timer. The regular press
    // that precedes this event is still forwarded by mousePressEvent above,
    // so the camera briefly sees a press-drag start immediately before the
    // zoom-to-location animation -- harmless, but a deliberate difference
    // from the GLFW example worth knowing about.
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::DOUBLE_CLICK;
    input_event.button = ToEarthMapButton(event->button());
    input_event.x = static_cast<float>(event->position().x());
    input_event.y = static_cast<float>(event->position().y());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::wheelEvent(QWheelEvent* event) {
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::MOUSE_SCROLL;
    input_event.scroll_delta = static_cast<float>(event->angleDelta().y()) / kQtWheelUnitsPerNotch;
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::KEY_PRESS;
    input_event.key = ToEarthMapKeyCode(event->key());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::keyReleaseEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::KEY_RELEASE;
    input_event.key = ToEarthMapKeyCode(event->key());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

namespace earth_map_qt_detail {

// Owns the earth_map::EarthMap instance and does the actual GL work.
// Lives on the render thread for its entire lifetime (constructed lazily
// in EarthMapQuickItem::sync(), destroyed via the QRunnable jobs below) --
// this is what makes it safe for CameraController::ProcessInput()/Update()
// to run here without locking against the GUI thread.
class EarthMapRenderer final : public QObject {
    Q_OBJECT

public:
    void SetWindow(QQuickWindow* window) { window_ = window; }
    void SetViewportRect(const QRect& rect) { viewport_rect_ = rect; }
    void SetVisible(bool visible) { visible_ = visible; }

    void AppendPendingEvents(std::vector<earth_map::InputEvent> events) {
        pending_events_.insert(pending_events_.end(), std::make_move_iterator(events.begin()),
                                std::make_move_iterator(events.end()));
    }

public slots:
    void init() {
        if (earth_map_) {
            return;
        }

#ifndef __ANDROID__
        // GLEW's default extension query (glGetString(GL_EXTENSIONS)) is
        // invalid on some contexts Qt Quick's OpenGL RHI backend creates;
        // glewExperimental switches GLEW to the glGetStringi-based query.
        // Not applicable on Android: GLES entry points are directly
        // linked, no loader/init step needed.
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) {
            qFatal("EarthMapRenderer: glewInit() failed");
        }
#endif

        earth_map::Configuration config;
        config.screen_width = static_cast<std::uint32_t>(std::max(1, viewport_rect_.width()));
        config.screen_height = static_cast<std::uint32_t>(std::max(1, viewport_rect_.height()));
        // tile_provider left unset: EarthMapImpl::Initialize() falls back
        // to TileProviders::OpenStreetMap (src/core/earth_map_impl.cpp).

        try {
            earth_map_ = earth_map::EarthMap::Create(config);
        } catch (const std::exception& e) {
            qFatal("EarthMapRenderer: earth_map::EarthMap::Create() threw: %s", e.what());
        }
        if (!earth_map_ || !earth_map_->Initialize()) {
            qFatal("EarthMapRenderer: earth_map::EarthMap::Initialize() failed");
        }

        frame_timer_.start();
    }

    void paint() {
        if (!earth_map_ || !visible_ || viewport_rect_.width() <= 0 || viewport_rect_.height() <= 0) {
            pending_events_.clear();
            return;
        }

        // Squircle's own pattern for interleaving raw GL with Qt Quick's
        // RHI-recorded command stream (see squircle.cpp).
        window_->beginExternalCommands();

        // earth_map is drawn straight into the window's own render target
        // (no private FBO), so it must be scissor-clipped to this item's
        // own screen rect: Render() calls glClear(), which ignores
        // glViewport and is bounded only by GL_SCISSOR_TEST. Without this,
        // an embedded (non-fullscreen) globe panel would wipe out sibling
        // QML content drawn earlier in the same frame.
        glEnable(GL_SCISSOR_TEST);
        glScissor(viewport_rect_.x(), viewport_rect_.y(), viewport_rect_.width(),
                  viewport_rect_.height());

        earth_map::CameraController* camera = earth_map_->GetCameraController();
        if (camera) {
            for (const auto& event : pending_events_) {
                camera->ProcessInput(event);
            }
        }
        pending_events_.clear();

        const float delta_time_seconds = static_cast<float>(frame_timer_.restart()) / 1000.0f;
        if (camera) {
            camera->Update(delta_time_seconds);
        }

        // Renderer::Resize() (src/renderer/renderer.cpp) sets
        // glViewport(0, 0, w, h) -- a (0,0)-origin viewport, since it has
        // no notion of being embedded at an offset. Call it first, then
        // set our own offset viewport afterward so it wins; Render() only
        // reads the currently-bound GL_VIEWPORT, never sets it.
        earth_map_->Resize(static_cast<std::uint32_t>(viewport_rect_.width()),
                            static_cast<std::uint32_t>(viewport_rect_.height()));
        glViewport(viewport_rect_.x(), viewport_rect_.y(), viewport_rect_.width(),
                   viewport_rect_.height());

        earth_map_->Render();

        // Reset state that would otherwise bleed into the rest of the Qt
        // Quick scene graph's own (2D, depth-test-free, unscissored)
        // rendering.
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        window_->endExternalCommands();

        // earth_map animates continuously (camera inertia, tile fade-in),
        // so keep requesting the next frame. Safe to call from the render
        // thread -- see QQuickWindow::update()'s documentation.
        window_->update();
    }

private:
    QQuickWindow* window_ = nullptr;
    QRect viewport_rect_;
    bool visible_ = true;
    std::unique_ptr<earth_map::EarthMap> earth_map_;
    std::vector<earth_map::InputEvent> pending_events_;
    QElapsedTimer frame_timer_;
};

// Deletes the renderer on the render thread with a current GL context --
// required, since ~EarthMap() releases GL resources. Matches Squircle's
// CleanupJob exactly (see squircle.cpp).
class CleanupJob final : public QRunnable {
public:
    explicit CleanupJob(EarthMapRenderer* renderer) : renderer_(renderer) {}
    void run() override { delete renderer_; }

private:
    EarthMapRenderer* renderer_;
};

}  // namespace earth_map_qt_detail

void EarthMapQuickItem::sync() {
    if (!renderer_) {
        renderer_ = new earth_map_qt_detail::EarthMapRenderer();
        connect(window(), &QQuickWindow::beforeRendering, renderer_,
                &earth_map_qt_detail::EarthMapRenderer::init, Qt::DirectConnection);
        connect(window(), &QQuickWindow::beforeRenderPassRecording, renderer_,
                &earth_map_qt_detail::EarthMapRenderer::paint, Qt::DirectConnection);
    }

    renderer_->SetWindow(window());
    renderer_->SetViewportRect(DeviceViewportRect());
    renderer_->SetVisible(isVisible());
    renderer_->AppendPendingEvents(TakePendingEvents());
}

void EarthMapQuickItem::cleanup() {
    delete renderer_;
    renderer_ = nullptr;
}

void EarthMapQuickItem::releaseResources() {
    window()->scheduleRenderJob(new earth_map_qt_detail::CleanupJob(renderer_),
                                 QQuickWindow::BeforeSynchronizingStage);
    renderer_ = nullptr;
}

void EarthMapQuickItem::handleWindowChanged(QQuickWindow* window) {
    if (window) {
        connect(window, &QQuickWindow::beforeSynchronizing, this, &EarthMapQuickItem::sync,
                Qt::DirectConnection);
        connect(window, &QQuickWindow::sceneGraphInvalidated, this, &EarthMapQuickItem::cleanup,
                Qt::DirectConnection);
    }
}

#include "EarthMapQuickItem.moc"
