#pragma once

/**
 * @file frame_zone_timing_collector.h
 * @brief CPU+GPU per-zone frame timing, compiled in only when
 * EARTH_MAP_ENABLE_PERFORMANCE_MONITORING is defined.
 *
 * Internal implementation detail (lives in src/, not include/) -- not part
 * of the public API. Subsystems (Renderer, TileRenderer, ...) each own one
 * of these and expose only the resulting std::vector<FrameZoneTiming> (see
 * earth_map/renderer/performance_stats.h), which *is* public.
 *
 * Usage, once per frame:
 *   collector.BeginFrame();
 *   {
 *       EARTH_MAP_ZONE_SCOPE(collector, zone, "tile.draw");
 *       ...GL draw calls...
 *       zone.AddDrawCall(triangle_count);
 *   }
 *   auto zones = collector.EndFrame();
 *
 * When EARTH_MAP_ENABLE_PERFORMANCE_MONITORING is not defined, every type
 * and method here compiles to an empty no-op body with no member data --
 * instrumented code compiles identically to uninstrumented code, at zero
 * runtime cost.
 *
 * Zone names must come from a small, fixed set of compile-time string
 * literals (one collector instance is meant to see a handful of distinct
 * names over its whole lifetime, e.g. "tile.cull"/"tile.upload"/
 * "tile.draw"). Never derive a zone name from per-instance/per-object data
 * (e.g. a placemark ID) -- zone_state_/zone_order_ never evict entries, so
 * an unbounded set of names is an unbounded leak.
 */

#include <earth_map/renderer/performance_stats.h>

#ifdef EARTH_MAP_ENABLE_PERFORMANCE_MONITORING

#include "gpu_elapsed_time_query.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace earth_map {

class FrameZoneTimingCollector final {
public:
    void BeginFrame();
    std::vector<FrameZoneTiming> EndFrame();

    // Used by FrameZoneScope; not for direct use.
    void BeginZone(const std::string& name);
    void EndZone(const std::string& name);
    void AddDrawCall(const std::string& name, std::uint64_t triangle_count);

private:
    struct ZoneState {
        // One GPU query pair persists across frames (GL query objects are
        // relatively expensive to create/destroy); CPU timing is
        // per-frame only.
        std::unique_ptr<GpuElapsedTimeQuery> gpu_query;
    };

    std::unordered_map<std::string, ZoneState> zone_state_;
    // First-seen order, stable across frames, so output ordering doesn't
    // jump around from one frame to the next (std::unordered_map iteration
    // order isn't insertion order).
    std::vector<std::string> zone_order_;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> active_cpu_starts_;
    std::unordered_map<std::string, FrameZoneTiming> current_frame_zones_;
};

class FrameZoneScope final {
public:
    FrameZoneScope(FrameZoneTimingCollector& collector, std::string name)
        : collector_(collector), name_(std::move(name)) {
        collector_.BeginZone(name_);
    }
    ~FrameZoneScope() { collector_.EndZone(name_); }

    FrameZoneScope(const FrameZoneScope&) = delete;
    FrameZoneScope& operator=(const FrameZoneScope&) = delete;

    void AddDrawCall(std::uint64_t triangle_count) {
        collector_.AddDrawCall(name_, triangle_count);
    }

private:
    FrameZoneTimingCollector& collector_;
    std::string name_;
};

}  // namespace earth_map

#define EARTH_MAP_ZONE_SCOPE(collector, var, name) \
    ::earth_map::FrameZoneScope var((collector), (name))

#else  // !EARTH_MAP_ENABLE_PERFORMANCE_MONITORING

#include <cstdint>
#include <string>
#include <vector>

namespace earth_map {

class FrameZoneTimingCollector final {
public:
    void BeginFrame() {}
    std::vector<FrameZoneTiming> EndFrame() { return {}; }
};

class FrameZoneScope final {
public:
    FrameZoneScope(FrameZoneTimingCollector&, const std::string&) {}
    void AddDrawCall(std::uint64_t) {}
};

}  // namespace earth_map

#define EARTH_MAP_ZONE_SCOPE(collector, var, name) \
    ::earth_map::FrameZoneScope var((collector), (name))

#endif  // EARTH_MAP_ENABLE_PERFORMANCE_MONITORING
