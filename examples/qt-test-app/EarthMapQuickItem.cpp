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
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHoverEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRunnable>
#include <QSaveFile>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QTouchEvent>
#include <QVariant>
#include <QWheelEvent>

#include <earth_map/core/camera_controller.h>
#include <earth_map/constants.h>
#include <earth_map/earth_map.h>
#include <earth_map/renderer/renderer.h>
#include <earth_map/renderer/tile_renderer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

// Touch has no OS-level double-tap detection the way
// QQuickItem::mouseDoubleClickEvent() gets for real mouse clicks (Qt
// doesn't synthesize one for a QQuickItem that handles raw touch itself);
// it must be detected manually here, same idea as basic_example.cpp's own
// click-timer (its DOUBLE_CLICK_THRESHOLD is the same 300 ms).
constexpr std::uint64_t kDoubleTapThresholdMs = 300;
constexpr float kDoubleTapMaxDistancePx = 40.0f;

uint64_t NowMillis() {
    return static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
}

std::string InstallCaBundle() {
    const QString app_data_path =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (app_data_path.isEmpty() || !QDir().mkpath(app_data_path)) {
        qFatal("EarthMapRenderer: cannot create private app-data directory for CA bundle");
    }

    QFile bundled_bundle(":/earth-map/cacert.pem");
    if (!bundled_bundle.open(QIODevice::ReadOnly)) {
        qFatal("EarthMapRenderer: cannot open embedded CA bundle");
    }

    const QByteArray bundle_data = bundled_bundle.readAll();
    if (bundle_data.isEmpty()) {
        qFatal("EarthMapRenderer: embedded CA bundle is empty");
    }

    const QString installed_bundle_path = app_data_path + "/cacert.pem";
    QSaveFile installed_bundle(installed_bundle_path);
    if (!installed_bundle.open(QIODevice::WriteOnly) ||
        installed_bundle.write(bundle_data) != bundle_data.size() ||
        !installed_bundle.commit()) {
        qFatal("EarthMapRenderer: cannot install CA bundle in private app-data directory");
    }

    return installed_bundle_path.toStdString();
}

// ---------------------------------------------------------------------------
// Deterministic performance scenarios
//
// These live in the Qt test app rather than the library. They exercise the
// public camera/renderer APIs exactly as an embedding application would,
// while keeping benchmark policy (route, duration, report format) out of
// earth_map itself.
// ---------------------------------------------------------------------------

constexpr int kScenarioMaxZoom = 21;  // Must match TileRenderer's calibration.
constexpr double kScenarioWarmupSeconds = 2.0;
constexpr double kSteadyScenarioSeconds = 10.0;

double AltitudeMetersForScenarioZoom(int zoom) {
    return earth_map::constants::camera_constraints::MIN_ALTITUDE_METERS *
           static_cast<double>(1ULL << (kScenarioMaxZoom - zoom));
}

struct ScenarioWaypoint final {
    double longitude;
    double latitude;
    int target_zoom;
    double altitude_meters;
    double duration_seconds;
};

const std::array<ScenarioWaypoint, 8> kFlightScenarioWaypoints = {{
    {44.5152, 40.1872, 15, AltitudeMetersForScenarioZoom(15), 5.0},
    {44.5152, 40.1872, 8,  AltitudeMetersForScenarioZoom(8),  5.0},
    {45.0116, 40.4000, 14, AltitudeMetersForScenarioZoom(14), 5.0},
    {43.8478, 40.7894, 15, AltitudeMetersForScenarioZoom(15), 5.0},
    {43.8478, 40.7894, 8,  AltitudeMetersForScenarioZoom(8),  5.0},
    {44.4939, 40.8123, 14, AltitudeMetersForScenarioZoom(14), 5.0},
    {44.5453, 39.8814, 15, AltitudeMetersForScenarioZoom(15), 5.0},
    {44.5152, 40.1872, 8,  AltitudeMetersForScenarioZoom(8),  5.0},
}};

enum class PerformanceScenarioKind {
    None,
    SteadyZ13,
    Flight,
    FlightPreview,
};

PerformanceScenarioKind ParsePerformanceScenarioKind(const QString& name) {
    if (name == QStringLiteral("steady-z13")) {
        return PerformanceScenarioKind::SteadyZ13;
    }
    if (name == QStringLiteral("flight")) {
        return PerformanceScenarioKind::Flight;
    }
    if (name == QStringLiteral("flight-preview")) {
        return PerformanceScenarioKind::FlightPreview;
    }
    return PerformanceScenarioKind::None;
}

QString PerformanceScenarioName(PerformanceScenarioKind kind) {
    switch (kind) {
    case PerformanceScenarioKind::SteadyZ13:
        return QStringLiteral("steady-z13");
    case PerformanceScenarioKind::Flight:
        return QStringLiteral("flight");
    case PerformanceScenarioKind::FlightPreview:
        return QStringLiteral("flight-preview");
    case PerformanceScenarioKind::None:
        break;
    }
    return QStringLiteral("unknown");
}

bool IsRecordingPerformanceScenario(PerformanceScenarioKind kind) {
    return kind == PerformanceScenarioKind::SteadyZ13 ||
           kind == PerformanceScenarioKind::Flight;
}

struct ScenarioSample final {
    double elapsed_seconds = 0.0;
    double app_cpu_ms = 0.0;
    earth_map::PerformanceStats performance;
    earth_map::TileRenderStats tile;
};

struct PerformanceScenarioState final {
    enum class Phase {
        Inactive,
        Warmup,
        Measuring,
        PreviewingFlight,
    };

    PerformanceScenarioKind kind = PerformanceScenarioKind::None;
    Phase phase = Phase::Inactive;
    bool active = false;
    bool started_this_frame = false;
    bool finish_after_frame = false;
    bool completed = false;
    double warmup_elapsed_seconds = 0.0;
    double measurement_elapsed_seconds = 0.0;
    double waypoint_elapsed_seconds = 0.0;
    std::size_t waypoint_index = 0;
    QString started_at_utc;
    QString finish_reason;
    std::vector<ScenarioSample> samples;
};

double Quantile(std::vector<double> values, double percentile) {
    if (values.empty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    const double position = percentile * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return values[lower] + (values[upper] - values[lower]) * fraction;
}

QJsonObject SummarizeSamples(const std::vector<double>& values) {
    QJsonObject summary;
    summary[QStringLiteral("count")] = static_cast<int>(values.size());
    if (values.empty()) {
        return summary;
    }

    const auto [minimum, maximum] = std::minmax_element(values.begin(), values.end());
    summary[QStringLiteral("min_ms")] = *minimum;
    summary[QStringLiteral("p50_ms")] = Quantile(values, 0.50);
    summary[QStringLiteral("p95_ms")] = Quantile(values, 0.95);
    summary[QStringLiteral("max_ms")] = *maximum;
    return summary;
}

QString GlString(GLenum name) {
    const auto* text = reinterpret_cast<const char*>(glGetString(name));
    return text ? QString::fromUtf8(text) : QStringLiteral("unavailable");
}

}  // namespace

EarthMapQuickItem::EarthMapQuickItem(QQuickItem* parent) : QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setAcceptTouchEvents(true);
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
    // The render-thread scenario also discards queued input as a final
    // safeguard. Reject it here as well so a long touch gesture cannot grow
    // the GUI-to-render queue while the benchmark owns the camera.
    if (performance_scenario_active_) {
        return;
    }

    pending_events_.push_back(event);
    // Matches Squircle::setT() in Qt's own "Scene Graph - OpenGL Under QML"
    // example: request a sync+render cycle the moment new GUI-thread state
    // arrives, from the GUI thread, rather than relying solely on paint()'s
    // own window_->update() call at the end of each frame to eventually
    // pick it up.
    if (window()) {
        window()->update();
    }
}

std::vector<earth_map::InputEvent> EarthMapQuickItem::TakePendingEvents() {
    return std::exchange(pending_events_, {});
}

void EarthMapQuickItem::startPerformanceScenario(const QString& name) {
    if (performance_scenario_active_) {
        return;
    }
    if (ParsePerformanceScenarioKind(name) == PerformanceScenarioKind::None) {
        performance_scenario_status_ =
            QStringLiteral("Unknown performance scenario: %1").arg(name);
        emit performanceScenarioChanged();
        return;
    }

    performance_scenario_active_ = true;
    performance_scenario_status_ = QStringLiteral("%1: queued").arg(name);
    performance_scenario_report_path_.clear();
    pending_performance_scenario_name_ = name;
    performance_scenario_stop_requested_ = false;
    pending_events_.clear();
    emit performanceScenarioChanged();
    if (window()) {
        window()->update();
    }
}

void EarthMapQuickItem::stopPerformanceScenario() {
    if (!performance_scenario_active_) {
        return;
    }

    pending_performance_scenario_name_.clear();
    performance_scenario_stop_requested_ = true;
    performance_scenario_status_ = QStringLiteral("Stopping performance scenario…");
    emit performanceScenarioChanged();
    if (window()) {
        window()->update();
    }
}

void EarthMapQuickItem::mousePressEvent(QMouseEvent* event) {
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::MOUSE_BUTTON_PRESS;
    input_event.button = ToEarthMapButton(event->button());
    input_event.x = static_cast<float>(event->scenePosition().x());
    input_event.y = static_cast<float>(event->scenePosition().y());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::mouseReleaseEvent(QMouseEvent* event) {
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::MOUSE_BUTTON_RELEASE;
    input_event.button = ToEarthMapButton(event->button());
    input_event.x = static_cast<float>(event->scenePosition().x());
    input_event.y = static_cast<float>(event->scenePosition().y());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::mouseMoveEvent(QMouseEvent* event) {
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::MOUSE_MOVE;
    input_event.x = static_cast<float>(event->scenePosition().x());
    input_event.y = static_cast<float>(event->scenePosition().y());
    input_event.timestamp = NowMillis();
    QueueEvent(input_event);
    event->accept();
}

void EarthMapQuickItem::hoverMoveEvent(QHoverEvent* event) {
    earth_map::InputEvent input_event;
    input_event.type = earth_map::InputEvent::Type::MOUSE_MOVE;
    input_event.x = static_cast<float>(event->scenePosition().x());
    input_event.y = static_cast<float>(event->scenePosition().y());
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
    input_event.x = static_cast<float>(event->scenePosition().x());
    input_event.y = static_cast<float>(event->scenePosition().y());
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

void EarthMapQuickItem::touchEvent(QTouchEvent* event) {
    const auto& points = event->points();
    if (points.isEmpty()) {
        if (touch_drag_active_) {
            earth_map::InputEvent input_event;
            input_event.type = earth_map::InputEvent::Type::MOUSE_BUTTON_RELEASE;
            input_event.button = 0;
            input_event.timestamp = NowMillis();
            QueueEvent(input_event);
        }
        touch_drag_active_ = false;
        pinch_active_ = false;
        pinch_distance_ = 0.0f;
        event->accept();
        return;
    }

    std::array<const QEventPoint*, 2> active_points{};
    std::size_t active_point_count = 0;
    for (const QEventPoint& point : points) {
        if (point.state() == QEventPoint::Released ||
            point.state() == QEventPoint::Unknown) {
            continue;
        }
        if (active_point_count < active_points.size()) {
            active_points[active_point_count] = &point;
        }
        ++active_point_count;
    }

    const auto queue_drag_event = [this](const QEventPoint& point,
                                         earth_map::InputEvent::Type type) {
        earth_map::InputEvent input_event;
        input_event.type = type;
        input_event.button = 0;  // left mouse button / globe rotation drag
        input_event.x = static_cast<float>(point.scenePosition().x());
        input_event.y = static_cast<float>(point.scenePosition().y());
        input_event.timestamp = NowMillis();
        QueueEvent(input_event);
    };

    if (active_point_count >= 2) {
        const QPointF separation = active_points[0]->scenePosition() -
                                   active_points[1]->scenePosition();
        const float distance = static_cast<float>(std::hypot(separation.x(), separation.y()));

        if (!pinch_active_) {
            // A second finger must not leave the camera in drag mode while
            // it is being pinched.
            if (touch_drag_active_) {
                queue_drag_event(*active_points[0],
                                 earth_map::InputEvent::Type::MOUSE_BUTTON_RELEASE);
                touch_drag_active_ = false;
            }
            pinch_active_ = true;
            pinch_distance_ = distance;
        } else if (distance > 0.0f && pinch_distance_ > 0.0f) {
            // Scroll deltas are multiplicative in the camera controller, so
            // use the logarithmic change in finger separation. This makes a
            // 10% pinch feel the same at every absolute finger spacing.
            constexpr float kPinchZoomSensitivity = 4.0f;
            const float delta = std::clamp(
                std::log(distance / pinch_distance_) * kPinchZoomSensitivity,
                -1.0f,
                1.0f);
            if (delta != 0.0f) {
                earth_map::InputEvent input_event;
                input_event.type = earth_map::InputEvent::Type::MOUSE_SCROLL;
                input_event.scroll_delta = delta;
                input_event.timestamp = NowMillis();
                QueueEvent(input_event);
            }
            pinch_distance_ = distance;
        }
    } else if (active_point_count == 1) {
        const QEventPoint& point = *active_points[0];
        if (pinch_active_) {
            // Continue naturally as a one-finger drag once one finger of a
            // pinch is lifted.
            pinch_active_ = false;
            pinch_distance_ = 0.0f;
            queue_drag_event(point, earth_map::InputEvent::Type::MOUSE_BUTTON_PRESS);
            touch_drag_active_ = true;
        } else if (point.state() == QEventPoint::Pressed) {
            const std::uint64_t now = NowMillis();
            const QPointF position = point.scenePosition();
            const float distance_from_last_tap = std::hypot(
                static_cast<float>(position.x() - last_tap_position_.x()),
                static_cast<float>(position.y() - last_tap_position_.y()));
            if (has_last_tap_ && (now - last_tap_time_ms_) <= kDoubleTapThresholdMs &&
                distance_from_last_tap <= kDoubleTapMaxDistancePx) {
                earth_map::InputEvent double_tap_event;
                double_tap_event.type = earth_map::InputEvent::Type::DOUBLE_CLICK;
                double_tap_event.button = 0;
                double_tap_event.x = static_cast<float>(position.x());
                double_tap_event.y = static_cast<float>(position.y());
                double_tap_event.timestamp = now;
                QueueEvent(double_tap_event);
                has_last_tap_ = false;  // consumed; a third rapid tap starts fresh
            } else {
                has_last_tap_ = true;
                last_tap_time_ms_ = now;
                last_tap_position_ = position;
            }

            queue_drag_event(point, earth_map::InputEvent::Type::MOUSE_BUTTON_PRESS);
            touch_drag_active_ = true;
        } else if (point.state() == QEventPoint::Updated && touch_drag_active_) {
            queue_drag_event(point, earth_map::InputEvent::Type::MOUSE_MOVE);
        }
    } else {
        if (touch_drag_active_) {
            queue_drag_event(points.first(), earth_map::InputEvent::Type::MOUSE_BUTTON_RELEASE);
        }
        touch_drag_active_ = false;
        pinch_active_ = false;
        pinch_distance_ = 0.0f;
    }

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

    void RequestPerformanceScenarioStart(const QString& name) {
        pending_scenario_start_ = name;
        pending_scenario_stop_ = false;
    }

    void RequestPerformanceScenarioStop() {
        pending_scenario_start_.clear();
        pending_scenario_stop_ = true;
    }

signals:
    // Mirrors earth_map::PerformanceStats (see EarthMapQuickItem.h's
    // Q_PROPERTYs), pre-converted to QML-friendly types here on the render
    // thread since PerformanceStats/FrameZoneTiming are not QVariant-aware.
    void performanceStatsReady(int fps, double frameCpuMs, double frameGpuMs, bool hasFrameGpuMs,
                               QVariantList zoneTimings);

    // Mirrors earth_map::TileRenderStats (see EarthMapQuickItem.h's
    // Q_PROPERTYs), pre-converted to QML-friendly types here on the render
    // thread for the same reason as performanceStatsReady above.
    void tileRenderStatsReady(int visibleTiles, int renderedTiles, double averageLod,
                              int occupiedPoolLayers, int maxPoolLayers, double tilePoolBytesUsed,
                              double tilePoolBytesMax, double indirectionBytesUsed);

    // TEMPORARY, investigation only -- naive app-side measurement wrapping
    // Render(), independent of anything earth_map itself computes. Point is
    // to cross-check performanceStatsReady's fps/frameCpuMs against a
    // second, dumber measurement while mangohud is unavailable. Remove
    // once cross-checked.
    void appMeasuredStatsReady(double cpuMs, int fps);

    // Emitted only when a scenario changes state (queued/start/completed),
    // never once per frame. That keeps the QML performance overlay out of
    // the benchmarked frame loop.
    void performanceScenarioStateReady(bool active, QString status, QString reportPath);

public slots:
    void init() {
        if (earth_map_) {
            return;
        }

        // This item does all its rendering via raw GL calls interleaved
        // into Qt Quick's own command stream (beginExternalCommands()/
        // endExternalCommands() in paint()) -- only valid when the scene
        // graph is actually using the OpenGL RHI backend. main.cpp pins
        // this via QQuickWindow::setGraphicsApi(QSGRendererInterface::
        // OpenGL) before any window is created, so this should never fire;
        // it exists so a future change to that call fails loudly here
        // instead of corrupting GL state silently. Matches Qt's own
        // "OpenGL Under QML" example (squircle.cpp's SquircleRenderer::init()).
        Q_ASSERT(window_->rendererInterface()->graphicsApi() == QSGRendererInterface::OpenGL);

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
#ifdef __ANDROID__
        // Android's native trust store is not automatically visible to the
        // packaged OpenSSL/libcurl backend. Keep strict TLS verification and
        // point libcurl at the current Mozilla CA bundle embedded above.
        config.tile_loader_config.ca_cert_path = InstallCaBundle();
#endif

        // Matches examples/basic_example.cpp's googleProvider exactly,
        // including the placeholder API key -- Google's tile endpoint
        // needs a real key to actually serve imagery; this just mirrors
        // basic_example's provider wiring, not a working Google Maps key.
        auto google_provider = std::make_shared<earth_map::BasicXYZTileProvider>(
            "GoogleMaps",
            "https://mt{s}.google.com/vt/lyrs=m&x={x}&y={y}&z={z}&key=YOUR_API_KEY",
            "0123",  // Subdomains for load balancing
            0,       // Min zoom
            21,      // Max zoom
            "png");
        config.tile_provider = google_provider;

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

        const float delta_time_seconds = static_cast<float>(frame_timer_.restart()) / 1000.0f;
        earth_map::CameraController* camera = earth_map_->GetCameraController();
        ProcessPerformanceScenarioCommands(camera);

        // A scripted benchmark or preview owns the camera for its complete
        // route. Drop queued interactive events rather than mixing a touch
        // gesture into a deterministic camera flight.
        if (performance_scenario_.active) {
            pending_events_.clear();
        } else if (camera) {
            for (const auto& event : pending_events_) {
                camera->ProcessInput(event);
            }
            pending_events_.clear();
        } else {
            pending_events_.clear();
        }

        AdvancePerformanceScenario(camera, delta_time_seconds);
        if (camera) {
            camera->Update(delta_time_seconds);
        }

        // Renderer::Resize() (src/renderer/renderer.cpp) sets
        // glViewport(0, 0, w, h) -- a (0,0)-origin viewport, since it has
        // no notion of being embedded at an offset. Call it first, then
        // set our own offset viewport afterward so it wins; Render() only
        // reads the currently-bound GL_VIEWPORT, never sets it.
        // earth_map_->Resize(static_cast<std::uint32_t>(viewport_rect_.width()),
        //                     static_cast<std::uint32_t>(viewport_rect_.height()));
        glViewport(viewport_rect_.x(), viewport_rect_.y(), viewport_rect_.width(),
                   viewport_rect_.height());

        // TEMPORARY, investigation only -- see appMeasuredStatsReady's
        // declaration above. Naive wall-clock wrap of Render(), completely
        // independent of earth_map's own internal timing below.
        const auto app_cpu_start = std::chrono::steady_clock::now();
        earth_map_->Render();
        const auto app_cpu_end = std::chrono::steady_clock::now();
        const double app_cpu_ms =
            std::chrono::duration<double, std::milli>(app_cpu_end - app_cpu_start).count();

        // The globe is an OpenGL underlay in Qt Quick's main render pass.
        // Its color must remain, but its 3D depth values must not participate
        // in the following Qt Quick HUD draw calls: otherwise ordinary QML
        // rectangles and text can be depth-occluded by the globe depending
        // on the camera angle. GL_SCISSOR_TEST is still enabled for exactly
        // this item's viewport, so this leaves depth outside the map intact.
        glClear(GL_DEPTH_BUFFER_BIT);

        // earth_map::Renderer measures its own frame timing internally
        // (Renderer::EndFrame(), src/renderer/renderer.cpp) -- more
        // accurate than timing Render() from out here, and it's the only
        // source for fps/GPU ms/per-zone breakdown. fps/frame_cpu_ms are
        // always live; frame_gpu_ms and zones are only populated when
        // earth_map was built with EARTH_MAP_ENABLE_PERFORMANCE_MONITORING.
        const earth_map::PerformanceStats stats = earth_map_->GetRenderer()->GetStats();

        earth_map::TileRenderer* tile_renderer = earth_map_->GetRenderer()->GetTileRenderer();
        earth_map::TileRenderStats tile_stats;
        bool has_tile_stats = false;
        if (tile_renderer) {
            tile_stats = tile_renderer->GetStats();
            has_tile_stats = true;
        }

        if (performance_scenario_.phase == PerformanceScenarioState::Phase::Measuring) {
            RecordPerformanceScenarioSample(app_cpu_ms, stats, tile_stats);
        }
        if (performance_scenario_.finish_after_frame) {
            FinishPerformanceScenario();
        }

        // A recording benchmark suppresses frame-by-frame QML
        // instrumentation because the live chart and QVariant conversion
        // would otherwise become part of its measured frame cost. A flight
        // preview deliberately keeps these values live for visual inspection.
        if (!performance_scenario_.active ||
            !IsRecordingPerformanceScenario(performance_scenario_.kind)) {
            ++app_frames_this_window_;
            if (app_cpu_end - app_fps_window_start_ >= std::chrono::seconds(1)) {
                app_current_fps_ = static_cast<int>(app_frames_this_window_);
                app_frames_this_window_ = 0;
                app_fps_window_start_ = app_cpu_end;
            }
            emit appMeasuredStatsReady(app_cpu_ms, app_current_fps_);

            QVariantList zone_timings;
            zone_timings.reserve(static_cast<int>(stats.zones.size()));
            for (const auto& zone : stats.zones) {
                QVariantMap zone_entry;
                zone_entry["name"] = QString::fromStdString(zone.name);
                zone_entry["cpuMs"] = zone.cpu_ms;
                zone_entry["gpuMs"] = zone.gpu_ms ? *zone.gpu_ms : -1.0;
                zone_entry["hasGpuMs"] = zone.gpu_ms.has_value();
                zone_entry["drawCalls"] = static_cast<int>(zone.draw_calls);
                zone_entry["triangles"] = static_cast<double>(zone.triangles);
                zone_timings.append(zone_entry);
            }

            emit performanceStatsReady(static_cast<int>(stats.fps), stats.frame_cpu_ms,
                                        stats.frame_gpu_ms ? *stats.frame_gpu_ms : -1.0,
                                        stats.frame_gpu_ms.has_value(), zone_timings);
        }

        if (has_tile_stats &&
            (!performance_scenario_.active ||
             !IsRecordingPerformanceScenario(performance_scenario_.kind))) {
            emit tileRenderStatsReady(
                static_cast<int>(tile_stats.visible_tiles),
                static_cast<int>(tile_stats.rendered_tiles),
                static_cast<double>(tile_stats.average_lod),
                static_cast<int>(tile_stats.occupied_pool_layers),
                static_cast<int>(tile_stats.max_pool_layers),
                static_cast<double>(tile_stats.tile_pool_bytes_used),
                static_cast<double>(tile_stats.tile_pool_bytes_max),
                static_cast<double>(tile_stats.indirection_bytes_used));
        }

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
    void ProcessPerformanceScenarioCommands(earth_map::CameraController* camera) {
        if (!pending_scenario_start_.isEmpty()) {
            const QString requested_name = std::exchange(pending_scenario_start_, QString{});
            const PerformanceScenarioKind kind = ParsePerformanceScenarioKind(requested_name);
            if (kind == PerformanceScenarioKind::None) {
                emit performanceScenarioStateReady(
                    false,
                    QStringLiteral("Unknown performance scenario: %1").arg(requested_name),
                    {});
            } else if (!camera) {
                emit performanceScenarioStateReady(
                    false, QStringLiteral("Cannot start scenario: camera is unavailable"), {});
            } else if (performance_scenario_.active) {
                emit performanceScenarioStateReady(
                    true, QStringLiteral("A performance scenario is already running"), {});
            } else {
                StartPerformanceScenario(kind, *camera);
            }
        }

        if (pending_scenario_stop_) {
            pending_scenario_stop_ = false;
            if (performance_scenario_.active) {
                performance_scenario_.completed = false;
                performance_scenario_.finish_reason = QStringLiteral("stopped by user");
                performance_scenario_.finish_after_frame = true;
            }
        }
    }

    void StartPerformanceScenario(PerformanceScenarioKind kind,
                                  earth_map::CameraController& camera) {
        performance_scenario_ = {};
        performance_scenario_.kind = kind;
        performance_scenario_.active = true;
        performance_scenario_.started_this_frame = true;
        performance_scenario_.started_at_utc =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

        // All scripted routes begin from a known orbital camera state. The
        // steady benchmark starts at z13; both flight modes start at z8,
        // matching the basic example's streaming stress path.
        camera.SetMovementMode(earth_map::CameraController::MovementMode::ORBIT);
        const int start_zoom = kind == PerformanceScenarioKind::SteadyZ13 ? 13 : 8;
        camera.SetGeographicPosition(44.5152, 40.1872,
                                     AltitudeMetersForScenarioZoom(start_zoom));

        if (kind == PerformanceScenarioKind::FlightPreview) {
            performance_scenario_.phase = PerformanceScenarioState::Phase::PreviewingFlight;
            StartFlightWaypoint(camera);
            emit performanceScenarioStateReady(
                true, QStringLiteral("flight preview: live HUD, no report"), {});
            return;
        }

        performance_scenario_.phase = PerformanceScenarioState::Phase::Warmup;

        emit performanceScenarioStateReady(
            true,
            QStringLiteral("%1: warming up for %2 s")
                .arg(PerformanceScenarioName(kind))
                .arg(kScenarioWarmupSeconds, 0, 'f', 0),
            {});
    }

    void StartFlightWaypoint(earth_map::CameraController& camera) {
        const ScenarioWaypoint& waypoint =
            kFlightScenarioWaypoints[performance_scenario_.waypoint_index];
        camera.FlyTo(waypoint.longitude, waypoint.latitude, waypoint.altitude_meters,
                     static_cast<float>(waypoint.duration_seconds));
    }

    void AdvancePerformanceScenario(earth_map::CameraController* camera,
                                    float delta_time_seconds) {
        if (!performance_scenario_.active || performance_scenario_.finish_after_frame) {
            return;
        }

        // The first frame after clicking Start includes arbitrary idle time
        // since the previous Qt frame. Do not charge it to the warm-up.
        if (performance_scenario_.started_this_frame) {
            performance_scenario_.started_this_frame = false;
            return;
        }

        const double delta_seconds = std::max(0.0, static_cast<double>(delta_time_seconds));
        if (performance_scenario_.phase == PerformanceScenarioState::Phase::Warmup) {
            performance_scenario_.warmup_elapsed_seconds += delta_seconds;
            if (performance_scenario_.warmup_elapsed_seconds < kScenarioWarmupSeconds) {
                return;
            }

            performance_scenario_.phase = PerformanceScenarioState::Phase::Measuring;
            performance_scenario_.measurement_elapsed_seconds = 0.0;
            performance_scenario_.samples.clear();
            if (performance_scenario_.kind == PerformanceScenarioKind::Flight && camera) {
                StartFlightWaypoint(*camera);
            }
            return;
        }

        if (performance_scenario_.phase != PerformanceScenarioState::Phase::PreviewingFlight) {
            performance_scenario_.measurement_elapsed_seconds += delta_seconds;
        }
        if (performance_scenario_.kind == PerformanceScenarioKind::SteadyZ13) {
            if (performance_scenario_.measurement_elapsed_seconds >= kSteadyScenarioSeconds) {
                performance_scenario_.completed = true;
                performance_scenario_.finish_reason = QStringLiteral("completed");
                performance_scenario_.finish_after_frame = true;
            }
            return;
        }

        performance_scenario_.waypoint_elapsed_seconds += delta_seconds;
        const ScenarioWaypoint& waypoint =
            kFlightScenarioWaypoints[performance_scenario_.waypoint_index];
        if (performance_scenario_.waypoint_elapsed_seconds < waypoint.duration_seconds) {
            return;
        }

        ++performance_scenario_.waypoint_index;
        if (performance_scenario_.waypoint_index >= kFlightScenarioWaypoints.size()) {
            performance_scenario_.completed = true;
            performance_scenario_.finish_reason = QStringLiteral("completed");
            performance_scenario_.finish_after_frame = true;
            return;
        }

        performance_scenario_.waypoint_elapsed_seconds = 0.0;
        if (camera) {
            StartFlightWaypoint(*camera);
        }
    }

    void RecordPerformanceScenarioSample(double app_cpu_ms,
                                         const earth_map::PerformanceStats& stats,
                                         const earth_map::TileRenderStats& tile_stats) {
        ScenarioSample sample;
        sample.elapsed_seconds = performance_scenario_.measurement_elapsed_seconds;
        sample.app_cpu_ms = app_cpu_ms;
        sample.performance = stats;
        sample.tile = tile_stats;
        performance_scenario_.samples.push_back(std::move(sample));
    }

    QJsonObject BuildPerformanceScenarioReport() const {
        QJsonObject report;
        report[QStringLiteral("schema_version")] = 1;
        report[QStringLiteral("report_type")] = QStringLiteral("earth_map.qt.performance");
        report[QStringLiteral("scenario")] = PerformanceScenarioName(performance_scenario_.kind);
        report[QStringLiteral("outcome")] = performance_scenario_.completed
                                                  ? QStringLiteral("completed")
                                                  : QStringLiteral("stopped");
        report[QStringLiteral("reason")] = performance_scenario_.finish_reason;
        report[QStringLiteral("started_at_utc")] = performance_scenario_.started_at_utc;
        report[QStringLiteral("finished_at_utc")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        report[QStringLiteral("warmup_seconds")] = kScenarioWarmupSeconds;
        report[QStringLiteral("measurement_seconds")] =
            performance_scenario_.measurement_elapsed_seconds;
        report[QStringLiteral("percentile_method")] =
            QStringLiteral("linear interpolation over sorted samples");

        QJsonObject benchmark_environment;
        benchmark_environment[QStringLiteral("network")] =
            QStringLiteral("caller-controlled; record it with the report");
        benchmark_environment[QStringLiteral("performance_overlay")] =
            QStringLiteral("suppressed while scenario is active");
        report[QStringLiteral("benchmark_environment")] = benchmark_environment;

        QJsonObject viewport;
        viewport[QStringLiteral("width_px")] = viewport_rect_.width();
        viewport[QStringLiteral("height_px")] = viewport_rect_.height();
        report[QStringLiteral("viewport")] = viewport;

        QJsonObject graphics;
        graphics[QStringLiteral("vendor")] = GlString(GL_VENDOR);
        graphics[QStringLiteral("renderer")] = GlString(GL_RENDERER);
        graphics[QStringLiteral("version")] = GlString(GL_VERSION);
        report[QStringLiteral("graphics")] = graphics;

        QJsonArray samples;
        std::vector<double> app_cpu_values;
        std::vector<double> frame_cpu_values;
        std::vector<double> frame_gpu_values;
        std::vector<double> fps_values;
        std::map<QString, std::vector<double>> zone_cpu_values;
        std::map<QString, std::vector<double>> zone_gpu_values;
        std::map<QString, earth_map::FrameZoneTiming> latest_zone_timings;

        app_cpu_values.reserve(performance_scenario_.samples.size());
        frame_cpu_values.reserve(performance_scenario_.samples.size());
        frame_gpu_values.reserve(performance_scenario_.samples.size());
        fps_values.reserve(performance_scenario_.samples.size());

        for (const ScenarioSample& sample : performance_scenario_.samples) {
            QJsonObject sample_json;
            sample_json[QStringLiteral("elapsed_seconds")] = sample.elapsed_seconds;
            sample_json[QStringLiteral("app_cpu_ms")] = sample.app_cpu_ms;
            sample_json[QStringLiteral("fps")] = static_cast<int>(sample.performance.fps);
            sample_json[QStringLiteral("frame_cpu_ms")] = sample.performance.frame_cpu_ms;
            if (sample.performance.frame_gpu_ms) {
                sample_json[QStringLiteral("frame_gpu_ms")] = *sample.performance.frame_gpu_ms;
                frame_gpu_values.push_back(*sample.performance.frame_gpu_ms);
            }

            QJsonObject tile;
            tile[QStringLiteral("visible_tiles")] = static_cast<double>(sample.tile.visible_tiles);
            tile[QStringLiteral("ready_tiles")] = static_cast<double>(sample.tile.rendered_tiles);
            tile[QStringLiteral("occupied_pool_layers")] =
                static_cast<int>(sample.tile.occupied_pool_layers);
            tile[QStringLiteral("max_pool_layers")] = static_cast<int>(sample.tile.max_pool_layers);
            sample_json[QStringLiteral("tile")] = tile;

            QJsonArray zones;
            for (const earth_map::FrameZoneTiming& zone : sample.performance.zones) {
                QJsonObject zone_json;
                const QString zone_name = QString::fromStdString(zone.name);
                zone_json[QStringLiteral("name")] = zone_name;
                zone_json[QStringLiteral("cpu_ms")] = zone.cpu_ms;
                zone_json[QStringLiteral("draw_calls")] = static_cast<int>(zone.draw_calls);
                zone_json[QStringLiteral("triangles")] = static_cast<double>(zone.triangles);
                zone_cpu_values[zone_name].push_back(zone.cpu_ms);
                if (zone.gpu_ms) {
                    zone_json[QStringLiteral("gpu_ms")] = *zone.gpu_ms;
                    zone_gpu_values[zone_name].push_back(*zone.gpu_ms);
                }
                latest_zone_timings[zone_name] = zone;
                zones.append(zone_json);
            }
            sample_json[QStringLiteral("zones")] = zones;
            samples.append(sample_json);

            app_cpu_values.push_back(sample.app_cpu_ms);
            frame_cpu_values.push_back(sample.performance.frame_cpu_ms);
            fps_values.push_back(static_cast<double>(sample.performance.fps));
        }
        report[QStringLiteral("samples")] = samples;

        QJsonObject summary;
        summary[QStringLiteral("sample_count")] =
            static_cast<int>(performance_scenario_.samples.size());
        summary[QStringLiteral("app_cpu")] = SummarizeSamples(app_cpu_values);
        summary[QStringLiteral("frame_cpu")] = SummarizeSamples(frame_cpu_values);
        summary[QStringLiteral("frame_gpu")] = SummarizeSamples(frame_gpu_values);
        summary[QStringLiteral("fps")] = SummarizeSamples(fps_values);

        QJsonObject zones_summary;
        for (const auto& [name, values] : zone_cpu_values) {
            QJsonObject zone_summary;
            zone_summary[QStringLiteral("cpu")] = SummarizeSamples(values);
            const auto gpu_it = zone_gpu_values.find(name);
            if (gpu_it != zone_gpu_values.end()) {
                zone_summary[QStringLiteral("gpu")] = SummarizeSamples(gpu_it->second);
            }
            const auto timing_it = latest_zone_timings.find(name);
            if (timing_it != latest_zone_timings.end()) {
                const earth_map::FrameZoneTiming& timing = timing_it->second;
                QJsonObject gpu_timer;
                gpu_timer[QStringLiteral("supported")] = timing.gpu_timing_supported;
                gpu_timer[QStringLiteral("submitted")] =
                    static_cast<double>(timing.gpu_timer.submitted);
                gpu_timer[QStringLiteral("resolved")] =
                    static_cast<double>(timing.gpu_timer.resolved);
                gpu_timer[QStringLiteral("skipped_no_free_slot")] =
                    static_cast<double>(timing.gpu_timer.skipped_no_free_slot);
                gpu_timer[QStringLiteral("discarded_disjoint")] =
                    static_cast<double>(timing.gpu_timer.discarded_disjoint);
                zone_summary[QStringLiteral("gpu_timer")] = gpu_timer;
            }
            zones_summary[name] = zone_summary;
        }
        summary[QStringLiteral("zones")] = zones_summary;
        report[QStringLiteral("summary")] = summary;
        return report;
    }

    QString WritePerformanceScenarioReport(QString* error) const {
        const QString app_data_path =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (app_data_path.isEmpty()) {
            *error = QStringLiteral("Qt returned no writable app-data directory");
            return {};
        }

        QDir directory(app_data_path);
        if (!directory.mkpath(QStringLiteral("earth-map-performance"))) {
            *error = QStringLiteral("cannot create earth-map-performance directory");
            return {};
        }
        if (!directory.cd(QStringLiteral("earth-map-performance"))) {
            *error = QStringLiteral("cannot enter earth-map-performance directory");
            return {};
        }

        const QString filename = QStringLiteral("%1-%2.json")
                                     .arg(PerformanceScenarioName(performance_scenario_.kind))
                                     .arg(QDateTime::currentDateTimeUtc().toString(
                                         QStringLiteral("yyyyMMdd-HHmmsszzz")));
        const QString report_path = directory.filePath(filename);
        QSaveFile output(report_path);
        if (!output.open(QIODevice::WriteOnly)) {
            *error = QStringLiteral("cannot open %1").arg(report_path);
            return {};
        }

        const QByteArray contents =
            QJsonDocument(BuildPerformanceScenarioReport()).toJson(QJsonDocument::Indented);
        if (output.write(contents) != contents.size() || !output.commit()) {
            *error = QStringLiteral("cannot commit %1").arg(report_path);
            return {};
        }
        return report_path;
    }

    void FinishPerformanceScenario() {
        const bool completed = performance_scenario_.completed;
        const QString outcome = completed ? QStringLiteral("completed")
                                          : QStringLiteral("stopped");

        if (!IsRecordingPerformanceScenario(performance_scenario_.kind)) {
            performance_scenario_.active = false;
            performance_scenario_.phase = PerformanceScenarioState::Phase::Inactive;
            performance_scenario_.finish_after_frame = false;
            emit performanceScenarioStateReady(
                false, QStringLiteral("flight preview %1").arg(outcome), {});
            return;
        }

        QString write_error;
        const QString report_path = WritePerformanceScenarioReport(&write_error);
        performance_scenario_.active = false;
        performance_scenario_.phase = PerformanceScenarioState::Phase::Inactive;
        performance_scenario_.finish_after_frame = false;

        if (report_path.isEmpty()) {
            qWarning().noquote() << "EarthMap performance report failed:" << write_error;
            emit performanceScenarioStateReady(
                false, QStringLiteral("%1, but report write failed: %2").arg(outcome, write_error), {});
            return;
        }

        qInfo().noquote() << "EarthMap performance report:" << report_path;
        emit performanceScenarioStateReady(
            false, QStringLiteral("%1: %2 samples written").arg(
                       outcome).arg(static_cast<int>(performance_scenario_.samples.size())), report_path);
    }

    QQuickWindow* window_ = nullptr;
    QRect viewport_rect_;
    bool visible_ = true;
    std::unique_ptr<earth_map::EarthMap> earth_map_;
    std::vector<earth_map::InputEvent> pending_events_;
    QElapsedTimer frame_timer_;

    // TEMPORARY, investigation only -- see appMeasuredStatsReady's
    // declaration above.
    std::chrono::steady_clock::time_point app_fps_window_start_ = std::chrono::steady_clock::now();
    std::uint32_t app_frames_this_window_ = 0;
    int app_current_fps_ = 0;

    QString pending_scenario_start_;
    bool pending_scenario_stop_ = false;
    PerformanceScenarioState performance_scenario_;
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
        connect(renderer_, &earth_map_qt_detail::EarthMapRenderer::performanceStatsReady, this,
                &EarthMapQuickItem::setPerformanceStats);
        connect(renderer_, &earth_map_qt_detail::EarthMapRenderer::appMeasuredStatsReady, this,
                &EarthMapQuickItem::setAppMeasuredStats);
        connect(renderer_, &earth_map_qt_detail::EarthMapRenderer::tileRenderStatsReady, this,
                &EarthMapQuickItem::setTileRenderStats);
        connect(renderer_, &earth_map_qt_detail::EarthMapRenderer::performanceScenarioStateReady, this,
                &EarthMapQuickItem::setPerformanceScenarioState);
    }

    renderer_->SetWindow(window());
    renderer_->SetViewportRect(DeviceViewportRect());
    renderer_->SetVisible(isVisible());
    if (!pending_performance_scenario_name_.isEmpty()) {
        renderer_->RequestPerformanceScenarioStart(
            std::exchange(pending_performance_scenario_name_, QString{}));
    }
    if (performance_scenario_stop_requested_) {
        renderer_->RequestPerformanceScenarioStop();
        performance_scenario_stop_requested_ = false;
    }
    renderer_->AppendPendingEvents(TakePendingEvents());
}

void EarthMapQuickItem::setPerformanceStats(int fps, double frameCpuMs, double frameGpuMs,
                                            bool hasFrameGpuMs, const QVariantList& zoneTimings) {
    fps_ = fps;
    frame_cpu_ms_ = frameCpuMs;
    frame_gpu_ms_ = frameGpuMs;
    has_frame_gpu_ms_ = hasFrameGpuMs;
    zone_timings_ = zoneTimings;
    emit performanceStatsChanged();
}

// TEMPORARY, investigation only -- see EarthMapRenderer::appMeasuredStatsReady.
void EarthMapQuickItem::setAppMeasuredStats(double cpuMs, int fps) {
    app_cpu_ms_ = cpuMs;
    app_fps_ = fps;
    emit performanceStatsChanged();
}

void EarthMapQuickItem::setTileRenderStats(int visibleTiles, int renderedTiles, double averageLod,
                                           int occupiedPoolLayers, int maxPoolLayers,
                                           double tilePoolBytesUsed, double tilePoolBytesMax,
                                           double indirectionBytesUsed) {
    visible_tiles_ = visibleTiles;
    rendered_tiles_ = renderedTiles;
    average_lod_ = averageLod;
    occupied_pool_layers_ = occupiedPoolLayers;
    max_pool_layers_ = maxPoolLayers;
    tile_pool_bytes_used_ = tilePoolBytesUsed;
    tile_pool_bytes_max_ = tilePoolBytesMax;
    indirection_bytes_used_ = indirectionBytesUsed;
    emit tileRenderStatsChanged();
}

void EarthMapQuickItem::setPerformanceScenarioState(bool active, const QString& status,
                                                     const QString& reportPath) {
    performance_scenario_active_ = active;
    performance_scenario_status_ = status;
    performance_scenario_report_path_ = reportPath;
    emit performanceScenarioChanged();
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
