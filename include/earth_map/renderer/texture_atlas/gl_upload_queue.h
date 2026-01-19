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
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <deque>
#include <mutex>

namespace earth_map {

/**
 * @brief Command structure for uploading a tile texture to OpenGL
 *
 * Contains all data needed to upload a decoded tile image to the GPU.
 * Transferred from worker threads to GL thread via GLUploadQueue.
 */
struct GLUploadCommand {
    /// Tile coordinates (X, Y, Zoom)
    TileCoordinates coords;

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

    /**
     * @brief Default constructor
     */
    GLUploadCommand()
        : width(0), height(0), channels(0) {}

    /**
     * @brief Construct with tile coordinates
     */
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
 * - FIFO ordering ensures tiles are uploaded in request order
 * - Non-blocking TryPop() allows GL thread to budget upload time per frame
 * - Bounded memory via atlas eviction (managed by caller)
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

    // Movable
    GLUploadQueue(GLUploadQueue&&) noexcept = default;
    GLUploadQueue& operator=(GLUploadQueue&&) noexcept = default;

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
     * FIFO Guarantee: Commands are returned in the order they were pushed
     */
    std::unique_ptr<GLUploadCommand> TryPop();

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

    /// Internal queue storage (FIFO)
    std::deque<std::unique_ptr<GLUploadCommand>> queue_;
};

} // namespace earth_map
