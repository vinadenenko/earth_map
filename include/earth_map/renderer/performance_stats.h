#pragma once

/**
 * @file performance_stats.h
 * @brief Per-frame performance data: CPU/GPU timing broken down by
 * subsystem, plus the GPU work counters (draw calls, triangles) that go
 * with each.
 *
 * Replaces the old RenderStats, which was almost entirely unwired (most
 * fields were never assigned; draw_calls was a hardcoded constant). This
 * is the real thing: populated whenever
 * EARTH_MAP_ENABLE_PERFORMANCE_MONITORING is compiled in, and a true
 * zero-cost no-op (PerformanceStats stays default-constructed, nothing
 * measured) when it isn't -- see FrameZoneScope in
 * src/renderer/frame_zone_timing_collector.h for the instrumentation side
 * (an internal detail, not part of this public header).
 *
 * Subsystem-composition data (e.g. how many tiles are currently visible)
 * is deliberately not duplicated in here -- that lives in each
 * subsystem's own stats type (e.g. TileRenderStats, reachable via
 * TileRenderer::GetStats()). This type answers "where did frame time go",
 * not "what is resident right now".
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace earth_map {

/**
 * @brief CPU and GPU cost of one named subsystem phase within a frame.
 *
 * Zone names are dotted subsystem.phase strings (e.g. "tile.cull",
 * "tile.upload", "tile.draw", "terrain.draw", "minimap.draw"), chosen so
 * new subsystems (terrain, placemarks, video-on-terrain) can add their own
 * zones later without changing this type or anything that reads it.
 */
struct FrameZoneTiming {
    std::string name;

    /** Wall-clock CPU time spent in this zone this frame, in milliseconds. */
    double cpu_ms = 0.0;

    /**
     * GPU execution time for this zone, in milliseconds, or std::nullopt
     * when GPU timer queries aren't available (e.g. GL_EXT_disjoint_timer_
     * query missing on this Android device/driver) or haven't produced a
     * result yet.
     *
     * This is always the *previous* frame's GPU time, never the current
     * one: GL timer queries are asynchronous, and reading a query's result
     * before the GPU has actually finished it forces a CPU/GPU stall --
     * which would both hurt real performance and corrupt the very number
     * being measured. One frame of lag avoids that entirely in the
     * overwhelming common case.
     */
    std::optional<double> gpu_ms;

    /** Draw calls issued while this zone was active. */
    std::uint32_t draw_calls = 0;

    /** Triangles submitted while this zone was active. */
    std::uint64_t triangles = 0;
};

/**
 * @brief Per-frame performance snapshot for the whole renderer.
 *
 * Reachable via Renderer::GetStats(). See performance_stats.h's file
 * comment for why this replaced RenderStats and why subsystem-composition
 * data (visible tile counts, etc.) isn't duplicated in here.
 */
struct PerformanceStats {
    std::uint32_t fps = 0;

    /** Total CPU wall-clock time for the last frame's Renderer::Render()
      * call, in milliseconds. Sum of zones[*].cpu_ms plus any unattributed
      * overhead between zones. */
    double frame_cpu_ms = 0.0;

    /** Total GPU time for the last frame, in milliseconds -- see
      * FrameZoneTiming::gpu_ms for why this lags one frame behind
      * frame_cpu_ms, and why it can be std::nullopt. */
    std::optional<double> frame_gpu_ms;

    /** Per-subsystem breakdown. Order matches the order zones were
      * recorded in during Render(). */
    std::vector<FrameZoneTiming> zones;
};

}  // namespace earth_map
