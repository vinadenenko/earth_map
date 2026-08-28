#pragma once

/**
 * @file gl_upload_queue.h
 * @brief Thread-safe queue for OpenGL texture upload commands
 *
 * Provides a multi-producer, single-consumer (MPSC) queue for transferring
 * decoded tile image data from worker threads to the OpenGL rendering thread.
 * Worker threads push upload commands; the GL thread drains the queue.
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/imagery/tile_matrix_set.h>
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <deque>
#include <mutex>
#include <optional>
#include <chrono>
#include <unordered_map>

namespace earth_map {

/**
 * @brief Command structure for uploading a tile texture to OpenGL
 *
 * Contains all data needed to upload a decoded tile image to the GPU.
 * Transferred from worker threads to GL thread via GLUploadQueue.
 */
struct GLUploadCommand {
    /// Legacy renderer request address. This is retained only until
    /// geographic patches replace the icosphere selection path.
    TileCoordinates coords;

    /// Required source-aware identity for a successful imagery upload.
    std::optional<imagery::ImageTileKey> imagery_key;

    /// Decoded pixel data (RGB or RGBA)
    std::vector<std::uint8_t> pixel_data;

    /// Image width in pixels
    std::uint32_t width;

    /// Image height in pixels
    std::uint32_t height;

    /// Number of color channels (3 for RGB, 4 for RGBA)
    std::uint8_t channels;

    /// Optional callback executed after upload completes (on GL thread)
    std::function<void(const TileCoordinates&)> on_complete;

    /// Set by GLUploadQueue::Push so the render thread can measure how long
    /// decoded work waits before it is allowed to consume GL time.
    std::chrono::steady_clock::time_point enqueued_at{};

    /**
     * @brief Default constructor
     */
    GLUploadCommand()
        : width(0), height(0), channels(0) {}

    /** @brief Construct an empty failure command for a renderer request. */
    explicit GLUploadCommand(const TileCoordinates& tile_coords)
        : coords(tile_coords), width(0), height(0), channels(0) {}
};

/**
 * @brief Thread-safe queue for GL upload commands
 *
 * Multi-producer, single-consumer (MPSC) queue design:
 * - Multiple worker threads push decoded tile data (producers)
 * - Single OpenGL thread pops commands for upload (consumer)
 *
 * Implementation uses mutex + deque for simplicity and correctness.
 * Can be optimized to lock-free queue (e.g., moodycamel::ConcurrentQueue)
 * in future if profiling shows contention.
 *
 * Thread Safety:
 * - Push() is thread-safe (multiple producers)
 * - TryPop() is thread-safe (single consumer expected, but safe for multiple)
 * - Size() is thread-safe (approximate)
 *
 * Design Rationale:
 * - The current camera request set is admitted ahead of cache-warm decoded work
 * - Equal-priority uploads retain FIFO ordering for deterministic behavior
 * - Non-blocking TryPop() lets the GL thread impose a per-frame transfer budget
 */
class GLUploadQueue {
public:
    /**
     * @brief Constructor
     */
    GLUploadQueue() = default;

    /**
     * @brief Destructor
     */
    ~GLUploadQueue() = default;

    // Non-copyable
    GLUploadQueue(const GLUploadQueue&) = delete;
    GLUploadQueue& operator=(const GLUploadQueue&) = delete;

    // Non-movable (contains std::mutex)
    GLUploadQueue(GLUploadQueue&&) = delete;
    GLUploadQueue& operator=(GLUploadQueue&&) = delete;

    /**
     * @brief Push an upload command to the queue (thread-safe)
     *
     * Called by worker threads after decoding tile image data.
     * Transfers ownership of the command to the queue.
     *
     * @param cmd Unique pointer to upload command (moved into queue)
     *
     * Thread Safety: Safe to call from multiple threads concurrently
     */
    void Push(std::unique_ptr<GLUploadCommand> cmd);

    /**
     * @brief Try to pop an upload command from the queue (thread-safe, non-blocking)
     *
     * Called by GL thread to retrieve next command for upload.
     * Returns nullptr if queue is empty (non-blocking).
     *
     * @return Unique pointer to upload command, or nullptr if empty
     *
     * Thread Safety: Safe to call from multiple threads, but intended for single consumer
     * Ordering: Commands for the active view are returned before stale work;
     * equal-priority commands retain push order.
     */
    std::unique_ptr<GLUploadCommand> TryPop();

    /**
     * Replaces the decoded pages useful for the current camera view. Commands
     * outside that set remain queued as low-priority cache-warm work; they
     * are not destroyed by the GL thread. Retained commands receive the
     * supplied priority (lower is sooner).
     *
     * Subsequent Push calls receive the same active or low-priority class.
     * This is intentionally a render-thread scheduling policy: cancellation
     * belongs to work that has not begun decoding, while completed payloads
     * must never be destructed on the render thread.
     */
    void SetActivePriorities(
        std::unordered_map<TileCoordinates, int, TileCoordinatesHash> priorities);

    /**
     * @brief Get current queue size (thread-safe, approximate)
     *
     * Returns approximate size due to concurrent access.
     * Useful for monitoring and debugging, not for synchronization.
     *
     * @return Current number of commands in queue
     *
     * Thread Safety: Safe to call concurrently, but result may be stale
     */
    std::size_t Size() const;

    /**
     * @brief Check if queue is empty (thread-safe, approximate)
     *
     * @return true if queue appears empty, false otherwise
     *
     * Thread Safety: Safe but result may be stale due to concurrent access
     */
    bool Empty() const {
        return Size() == 0;
    }

private:
    /// Mutex protecting queue access
    mutable std::mutex mutex_;

    struct QueuedCommand {
        std::unique_ptr<GLUploadCommand> command;
        int priority = 0;
        std::uint64_t sequence = 0;
    };

    /// Internal queue storage. Selection is priority-first and FIFO on ties.
    std::deque<QueuedCommand> queue_;

    static constexpr int kInactivePriority = 2;

    /// Current render-view priorities. Empty means no filter has been set.
    std::unordered_map<TileCoordinates, int, TileCoordinatesHash> active_priorities_;
    bool active_filter_enabled_ = false;
    std::uint64_t next_sequence_ = 0;
};

} // namespace earth_map
