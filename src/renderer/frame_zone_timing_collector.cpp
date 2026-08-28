/**
 * @file frame_zone_timing_collector.cpp
 * @brief See frame_zone_timing_collector.h. Compiled only when
 * EARTH_MAP_ENABLE_PERFORMANCE_MONITORING is defined -- the header's #else
 * branch provides the no-op types otherwise, and this whole file is empty.
 */

#include "frame_zone_timing_collector.h"

#ifdef EARTH_MAP_ENABLE_PERFORMANCE_MONITORING

namespace earth_map {

void FrameZoneTimingCollector::BeginFrame() {
    active_cpu_starts_.clear();
    current_frame_zones_.clear();
}

void FrameZoneTimingCollector::BeginZone(const std::string& name, bool measure_gpu) {
    active_cpu_starts_[name] = std::chrono::steady_clock::now();

    auto it = zone_state_.find(name);
    if (it == zone_state_.end()) {
        zone_order_.push_back(name);
        ZoneState state;
        if (measure_gpu && GpuElapsedTimeQuery::IsSupported()) {
            state.gpu_query = std::make_unique<GpuElapsedTimeQuery>();
        }
        it = zone_state_.emplace(name, std::move(state)).first;
    }
    if (it->second.gpu_query) {
        it->second.gpu_query->Begin();
    }

    FrameZoneTiming& timing = current_frame_zones_[name];
    timing.name = name;
}

void FrameZoneTimingCollector::EndZone(const std::string& name) {
    const auto start_it = active_cpu_starts_.find(name);
    if (start_it == active_cpu_starts_.end()) {
        // EndZone without a matching BeginZone -- shouldn't happen given
        // FrameZoneScope's RAII pairing, but don't crash if it does.
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start_it->second;
    const double elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();

    auto& zone_state = zone_state_.at(name);
    if (zone_state.gpu_query) {
        zone_state.gpu_query->End();
    }

    current_frame_zones_[name].cpu_ms = elapsed_ms;
}

void FrameZoneTimingCollector::AddDrawCall(const std::string& name,
                                            std::uint64_t triangle_count) {
    auto it = current_frame_zones_.find(name);
    if (it == current_frame_zones_.end()) {
        return;
    }
    it->second.draw_calls += 1;
    it->second.triangles += triangle_count;
}

std::vector<FrameZoneTiming> FrameZoneTimingCollector::EndFrame() {
    std::vector<FrameZoneTiming> result;
    result.reserve(zone_order_.size());

    for (const auto& name : zone_order_) {
        FrameZoneTiming timing;
        const auto current_it = current_frame_zones_.find(name);
        if (current_it != current_frame_zones_.end()) {
            timing = current_it->second;
        } else {
            // Zone existed in a previous frame but wasn't entered this one
            // (e.g. mini-map disabled this frame) -- report zero CPU cost,
            // but still poll its GPU query below so a result pending from
            // when it *was* active gets consumed rather than leaking.
            timing.name = name;
        }

        auto& zone_state = zone_state_.at(name);
        if (zone_state.gpu_query) {
            timing.gpu_ms = zone_state.gpu_query->TryTakePreviousResultMs();
            timing.gpu_timing_supported = true;
            timing.gpu_timer = zone_state.gpu_query->GetDiagnostics();
        }

        result.push_back(std::move(timing));
    }

    return result;
}

}  // namespace earth_map

#endif  // EARTH_MAP_ENABLE_PERFORMANCE_MONITORING
