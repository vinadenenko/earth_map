#pragma once

/**
 * @file tile_load_worker_pool.h
 * @brief Worker pool for loading and decoding tile textures
 *
 * Manages multiple worker threads that:
 * 1. Pull tile load requests from a priority queue
 * 2. Check cache for tile data
 * 3. Download tile from network if cache miss
 * 4. Decode image data (stb_image)
 * 5. Create GL upload command
 * 6. Push to GL upload queue
 *
 * Design:
 * - Multi-threaded (configurable worker count, default 4)
 * - Priority-based request queue (lower number = higher priority)
 * - Automatic deduplication of requests
 * - Graceful shutdown
 * - No OpenGL calls (CPU work only)
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/data/tile_cache.h>
#include <earth_map/data/tile_loader.h>
#include <earth_map/renderer/texture_atlas/gl_upload_queue.h>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_set>
#include <atomic>
#include <functional>

namespace earth_map {

/**
 * @brief Tile load request structure
 */
struct TileLoadRequest {
    /// Tile coordinates to load
    TileCoordinates coords;

    /// Priority (lower number = higher priority)
    int priority;

    /// Completion callback (optional, called after upload command created)
    std::function<void(const TileCoordinates&)> on_complete;

    /**
     * @brief Default constructor
     */
    TileLoadRequest() : priority(0) {}

    /**
     * @brief Constructor with parameters
     */
    TileLoadRequest(
        const TileCoordinates& tile_coords,
        int prio,
        std::function<void(const TileCoordinates&)> callback = nullptr)
        : coords(tile_coords), priority(prio), on_complete(std::move(callback)) {}

    /**
     * @brief Comparison for priority queue (higher priority = lower number)
     */
    bool operator<(const TileLoadRequest& other) const {
        // Higher priority (lower number) should come first
        // std::priority_queue is a max-heap, so we invert the comparison
        return priority > other.priority;
    }
};

/**
 * @brief Worker pool for tile loading and decoding
 *
 * Manages worker threads that process tile load requests:
 * - Fetch tiles from cache or network
 * - Decode image data
 * - Create GL upload commands
 * - Push to GL upload queue
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Can submit requests from multiple threads
 * - Worker threads coordinate via mutex and condition variable
 *
 * Design Rationale:
 * - Separates CPU work (download/decode) from GPU work (upload)
 * - Priority queue ensures important tiles load first
 * - Deduplication prevents redundant work
 * - Graceful shutdown ensures no data loss
 */
class TileLoadWorkerPool {
public:
    /**
     * @brief Constructor
     *
     * @param cache Shared pointer to tile cache
     * @param loader Shared pointer to tile loader
     * @param upload_queue Shared pointer to GL upload queue
     * @param num_threads Number of worker threads (default: 4)
     */
    TileLoadWorkerPool(
        std::shared_ptr<TileCache> cache,
        std::shared_ptr<TileLoader> loader,
        std::shared_ptr<GLUploadQueue> upload_queue,
        int num_threads = 4);

    /**
     * @brief Destructor
     *
     * Triggers graceful shutdown of worker threads.
     */
    ~TileLoadWorkerPool();

    // Non-copyable
    TileLoadWorkerPool(const TileLoadWorkerPool&) = delete;
    TileLoadWorkerPool& operator=(const TileLoadWorkerPool&) = delete;

    // Non-movable
    TileLoadWorkerPool(TileLoadWorkerPool&&) = delete;
    TileLoadWorkerPool& operator=(TileLoadWorkerPool&&) = delete;

    /**
     * @brief Submit a tile load request
     *
     * Adds request to priority queue. If tile is already queued or processing,
     * request is ignored (deduplication).
     *
     * @param coords Tile coordinates
     * @param priority Priority (lower number = higher priority, default: 0)
     * @param on_complete Completion callback (optional)
     *
     * Thread Safety: Safe to call from multiple threads
     */
    void SubmitRequest(
        const TileCoordinates& coords,
        int priority = 0,
        std::function<void(const TileCoordinates&)> on_complete = nullptr);

    /**
     * @brief Shutdown worker pool
     *
     * Signals workers to stop processing and waits for them to finish.
     * Current tasks will complete gracefully.
     *
     * Thread Safety: Safe to call from any thread
     */
    void Shutdown();

    /**
     * @brief Get number of pending requests
     *
     * @return Number of requests in queue
     */
    std::size_t GetPendingCount() const;

    /**
     * @brief Check if shutdown has been requested
     *
     * @return true if shutdown is in progress
     */
    bool IsShutdown() const {
        return shutdown_flag_.load();
    }

private:
    /**
     * @brief Worker thread main loop
     *
     * Continuously processes requests from the queue until shutdown.
     */
    void WorkerThreadMain();

    /**
     * @brief Process a single tile load request
     *
     * @param request Tile load request to process
     */
    void ProcessRequest(const TileLoadRequest& request);

    /**
     * @brief Decode image data using stb_image
     *
     * @param tile_data Tile data containing raw image bytes
     * @return true if decode succeeded, false otherwise
     *
     * Note: Modifies tile_data in place (sets width, height, channels, data)
     */
    bool DecodeImage(TileData& tile_data);

    /// Tile cache (check before downloading)
    std::shared_ptr<TileCache> cache_;

    /// Tile loader (download from network)
    std::shared_ptr<TileLoader> loader_;

    /// GL upload queue (push decoded tiles for GPU upload)
    std::shared_ptr<GLUploadQueue> upload_queue_;

    /// Worker threads
    std::vector<std::thread> workers_;

    /// Shutdown flag (atomic)
    std::atomic<bool> shutdown_flag_;

    /// Mutex protecting request queue and in-flight set
    mutable std::mutex queue_mutex_;

    /// Condition variable for worker notification
    std::condition_variable queue_cv_;

    /// Priority queue of tile load requests
    std::priority_queue<TileLoadRequest> request_queue_;

    /// Set of tiles currently being processed (deduplication)
    std::unordered_set<TileCoordinates, TileCoordinatesHash> in_flight_;
};

} // namespace earth_map
