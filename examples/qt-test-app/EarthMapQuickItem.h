#pragma once

#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlEngine>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <vector>

#include <earth_map/renderer/camera.h>  // earth_map::InputEvent

// Original qt example uses QOpenGLFunctions.
// We don't, because EarthMap lib has ~380 kinds of gl calls
// Main reason: there will be no benefit of using this since
// Nothing about a GL context cares who resolved its function pointers

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

    // Mirrors earth_map::PerformanceStats (see
    // include/earth_map/renderer/performance_stats.h), translated into
    // QML-friendly types. frameGpuMs/zoneTimings' "gpuMs" entries are only
    // meaningful when their "hasGpuMs"/hasFrameGpuMs companion is true --
    // GPU timing is unavailable when the library was built without
    // EARTH_MAP_ENABLE_PERFORMANCE_MONITORING, or before the first frame's
    // GPU query result has landed.
    Q_PROPERTY(int fps READ fps NOTIFY performanceStatsChanged FINAL)
    Q_PROPERTY(double frameCpuMs READ frameCpuMs NOTIFY performanceStatsChanged FINAL)
    Q_PROPERTY(double frameGpuMs READ frameGpuMs NOTIFY performanceStatsChanged FINAL)
    Q_PROPERTY(bool hasFrameGpuMs READ hasFrameGpuMs NOTIFY performanceStatsChanged FINAL)
    Q_PROPERTY(QVariantList zoneTimings READ zoneTimings NOTIFY performanceStatsChanged FINAL)

    // TEMPORARY, investigation only -- a second, independent measurement of
    // Render()'s wall-clock CPU cost and paint() call frequency, computed
    // entirely in this example app (EarthMapRenderer::paint()), with no
    // relation to earth_map::PerformanceStats above. Point is to cross-check
    // fps/frameCpuMs against a naive measurement while mangohud is
    // unavailable. Remove appCpuMs/appFps once cross-checked.
    Q_PROPERTY(double appCpuMs READ appCpuMs NOTIFY performanceStatsChanged FINAL)
    Q_PROPERTY(int appFps READ appFps NOTIFY performanceStatsChanged FINAL)

    // Mirrors earth_map::TileRenderStats (see
    // include/earth_map/renderer/tile_renderer.h) -- composition/residency
    // data ("what is resident right now"), kept separate from
    // PerformanceStats above ("where did frame time go") because the
    // library itself keeps that same split. Bytes are exposed as double
    // (raw bytes) since QML/JS numbers are double-precision and these
    // values stay well under 2^53.
    Q_PROPERTY(int visibleTiles READ visibleTiles NOTIFY tileRenderStatsChanged FINAL)
    Q_PROPERTY(int renderedTiles READ renderedTiles NOTIFY tileRenderStatsChanged FINAL)
    Q_PROPERTY(double averageLod READ averageLod NOTIFY tileRenderStatsChanged FINAL)
    Q_PROPERTY(int occupiedPoolLayers READ occupiedPoolLayers NOTIFY tileRenderStatsChanged FINAL)
    Q_PROPERTY(int maxPoolLayers READ maxPoolLayers NOTIFY tileRenderStatsChanged FINAL)
    Q_PROPERTY(double tilePoolBytesUsed READ tilePoolBytesUsed NOTIFY tileRenderStatsChanged FINAL)
    Q_PROPERTY(double tilePoolBytesMax READ tilePoolBytesMax NOTIFY tileRenderStatsChanged FINAL)

    // The benchmark runner is deliberately owned by the render thread: it
    // controls CameraController there, records renderer statistics without
    // updating the QML profiling widgets every frame, and writes one report
    // after completion. These properties only expose its state to QML.
    Q_PROPERTY(bool performanceScenarioActive READ performanceScenarioActive
               NOTIFY performanceScenarioChanged FINAL)
    Q_PROPERTY(QString performanceScenarioStatus READ performanceScenarioStatus
               NOTIFY performanceScenarioChanged FINAL)
    Q_PROPERTY(QString performanceScenarioReportPath READ performanceScenarioReportPath
               NOTIFY performanceScenarioChanged FINAL)

public:
    explicit EarthMapQuickItem(QQuickItem* parent = nullptr);

    int fps() const { return fps_; }
    double frameCpuMs() const { return frame_cpu_ms_; }
    double frameGpuMs() const { return frame_gpu_ms_; }
    bool hasFrameGpuMs() const { return has_frame_gpu_ms_; }
    QVariantList zoneTimings() const { return zone_timings_; }

    double appCpuMs() const { return app_cpu_ms_; }
    int appFps() const { return app_fps_; }

    int visibleTiles() const { return visible_tiles_; }
    int renderedTiles() const { return rendered_tiles_; }
    double averageLod() const { return average_lod_; }
    int occupiedPoolLayers() const { return occupied_pool_layers_; }
    int maxPoolLayers() const { return max_pool_layers_; }
    double tilePoolBytesUsed() const { return tile_pool_bytes_used_; }
    double tilePoolBytesMax() const { return tile_pool_bytes_max_; }

    bool performanceScenarioActive() const { return performance_scenario_active_; }
    QString performanceScenarioStatus() const { return performance_scenario_status_; }
    QString performanceScenarioReportPath() const { return performance_scenario_report_path_; }

    // Scenario names include steady-z13, cold-start-z13, flight, jump-stress,
    // and flight-preview. The preview follows the flight route without
    // recording a report. The call is received on the GUI thread and forwarded
    // at the next Qt Quick synchronization point so CameraController remains
    // render-thread-owned.
    Q_INVOKABLE void startPerformanceScenario(const QString& name);
    Q_INVOKABLE void stopPerformanceScenario();

signals:
    void performanceStatsChanged();
    void tileRenderStatsChanged();
    void performanceScenarioChanged();
public slots:
    void sync();
    void cleanup();

    // Invoked (queued, cross-thread) from
    // earth_map_qt_detail::EarthMapRenderer::performanceStatsReady, emitted
    // from paint() on the render thread. zoneTimings entries are
    // QVariantMaps with keys: name (string), cpuMs (double), gpuMs
    // (double, valid only if hasGpuMs), hasGpuMs (bool), drawCalls (int),
    // triangles (double).
    void setPerformanceStats(int fps, double frameCpuMs, double frameGpuMs, bool hasFrameGpuMs,
                             const QVariantList& zoneTimings);

    // TEMPORARY, investigation only -- see appCpuMs/appFps above.
    void setAppMeasuredStats(double cpuMs, int fps);

    // Invoked (queued, cross-thread) from
    // earth_map_qt_detail::EarthMapRenderer::tileRenderStatsReady, emitted
    // from paint() on the render thread.
    void setTileRenderStats(int visibleTiles, int renderedTiles, double averageLod,
                            int occupiedPoolLayers, int maxPoolLayers, double tilePoolBytesUsed,
                            double tilePoolBytesMax);

    void setPerformanceScenarioState(bool active, const QString& status,
                                     const QString& reportPath);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    // earth_map has a generic InputEvent interface rather than platform
    // gesture handling. Single-finger touch becomes a left drag; a two-finger
    // pinch becomes the existing scroll-based zoom input.
    void touchEvent(QTouchEvent* event) override;

private slots:
    void handleWindowChanged(QQuickWindow* window);

private:
    int fps_ = 0;
    double frame_cpu_ms_ = 0.0;
    double frame_gpu_ms_ = -1.0;
    bool has_frame_gpu_ms_ = false;
    QVariantList zone_timings_;

    // TEMPORARY, investigation only -- see appCpuMs/appFps above.
    double app_cpu_ms_ = 0.0;
    int app_fps_ = 0;

    int visible_tiles_ = 0;
    int rendered_tiles_ = 0;
    double average_lod_ = 0.0;
    int occupied_pool_layers_ = 0;
    int max_pool_layers_ = 0;
    double tile_pool_bytes_used_ = 0.0;
    double tile_pool_bytes_max_ = 0.0;

    bool performance_scenario_active_ = false;
    QString performance_scenario_status_;
    QString performance_scenario_report_path_;
    QString pending_performance_scenario_name_;
    bool performance_scenario_stop_requested_ = false;

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
    bool touch_drag_active_ = false;
    bool pinch_active_ = false;
    float pinch_distance_ = 0.0f;

    // Manual double-tap detection -- see kDoubleTapThresholdMs/
    // kDoubleTapMaxDistancePx in EarthMapQuickItem.cpp.
    bool has_last_tap_ = false;
    std::uint64_t last_tap_time_ms_ = 0;
    QPointF last_tap_position_;
    earth_map_qt_detail::EarthMapRenderer* renderer_ = nullptr;
};
