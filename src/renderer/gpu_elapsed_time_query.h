#pragma once

/**
 * @file gpu_elapsed_time_query.h
 * @brief Non-blocking ring of GL elapsed-time queries for one performance zone.
 *
 * Internal implementation detail of the performance-monitoring
 * instrumentation (see earth_map/renderer/performance_stats.h and
 * frame_zone_timing_collector.h) -- not part of the public API.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <earth_map/renderer/performance_stats.h>

namespace earth_map {

/**
 * A ring of GL elapsed-time queries for a single zone.
 * Begin()/End() bracket the GL work to measure; TryTakePreviousResultMs()
 * returns the oldest available result, never the current one. Reading a
 * query before the GPU has actually finished it forces a CPU/GPU stall,
 * which both hurts real performance and corrupts the measurement. The ring
 * prevents a slow GPU from overwriting an unresolved query; if every slot is
 * in flight, that measurement is skipped and recorded in diagnostics.
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

    /** Begins timing on a free query slot, or records a skipped measurement
      * when the entire ring is still awaiting GPU completion. */
    void Begin();

    /** Ends timing on the buffer Begin() started. */
    void End();

    /** Returns the oldest query result if the GPU has finished it
     * (checked with a non-blocking availability query -- never stalls),
     * and clears it for reuse. std::nullopt if not supported, not ready
     * yet, or the driver flagged the result as disjoint/invalid. */
    std::optional<double> TryTakePreviousResultMs();

    /** Cumulative diagnostics since this query stream was created. */
    const GpuTimerQueryDiagnostics& GetDiagnostics() const { return diagnostics_; }

private:
    static constexpr std::size_t kQueryRingSize = 8;

    std::optional<std::size_t> FindFreeSlot();
    std::optional<std::size_t> FindOldestPendingSlot() const;

    std::array<std::uint32_t, kQueryRingSize> query_ids_{};
    std::array<bool, kQueryRingSize> query_pending_{};
    std::array<std::uint64_t, kQueryRingSize> submission_order_{};
    std::size_t next_write_index_ = 0;
    std::uint64_t next_submission_order_ = 1;
    int active_index_ = -1;
    bool initialized_ = false;
    GpuTimerQueryDiagnostics diagnostics_;
};

}  // namespace earth_map
