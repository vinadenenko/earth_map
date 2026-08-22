#pragma once

/**
 * @file gpu_elapsed_time_query.h
 * @brief Ping-ponged GL elapsed-time query for one named performance zone.
 *
 * Internal implementation detail of the performance-monitoring
 * instrumentation (see earth_map/renderer/performance_stats.h and
 * frame_zone_timing_collector.h) -- not part of the public API.
 */

#include <cstdint>
#include <optional>

namespace earth_map {

/**
 * One ping-ponged pair of GL elapsed-time queries for a single zone.
 * Begin()/End() bracket the GL work to measure; TryTakePreviousResultMs()
 * returns the *prior* frame's result, never the current one -- reading a
 * query before the GPU has actually finished it forces a CPU/GPU stall,
 * which both hurts real performance and corrupts the measurement. One
 * frame of lag avoids that in the overwhelming common case.
 *
 * Desktop: core GL_TIME_ELAPSED (promoted from ARB_timer_query, core since
 * GL 3.3 -- GLEW already loads these as ordinary core entry points).
 * Android: GL_EXT_disjoint_timer_query, a genuine optional extension (not
 * present on every device/driver), resolved at runtime via
 * eglGetProcAddress. IsSupported() reflects this; when unsupported, every
 * method here is a no-op and results are always std::nullopt.
 *
 * Also handles GL_GPU_DISJOINT_EXT on Android: if the driver reports the
 * GPU clock changed mid-measurement (e.g. thermal throttling), the result
 * is meaningless and is discarded rather than returned.
 */
class GpuElapsedTimeQuery final {
public:
    GpuElapsedTimeQuery();
    ~GpuElapsedTimeQuery();

    GpuElapsedTimeQuery(const GpuElapsedTimeQuery&) = delete;
    GpuElapsedTimeQuery& operator=(const GpuElapsedTimeQuery&) = delete;

    /** Whether GPU timer queries are usable at all on this platform/driver.
      * Checked once (Android: extension string; desktop: always true,
      * since GL_TIME_ELAPSED is core GL 3.3+). */
    static bool IsSupported();

    /** Begins timing on the buffer not currently awaiting readback. */
    void Begin();

    /** Ends timing on the buffer Begin() started. */
    void End();

    /** Returns the other buffer's result if the GPU has finished it
      * (checked with a non-blocking availability query -- never stalls),
      * and clears it for reuse. std::nullopt if not supported, not ready
      * yet, or the driver flagged the result as disjoint/invalid. */
    std::optional<double> TryTakePreviousResultMs();

private:
    std::uint32_t query_ids_[2] = {0, 0};
    bool query_pending_[2] = {false, false};
    int write_index_ = 0;
    bool initialized_ = false;
};

}  // namespace earth_map
