#pragma once

/**
 * @file tile_texture_coordinator.h
 * @brief Main coordinator for tile texture loading and atlas management
 *
 * Coordinates all components of the texture atlas system:
 * - TileLoadWorkerPool: Worker threads for loading/decoding
 * - GLUploadQueue: Queue for GL upload commands
 * - TextureAtlasManager: OpenGL atlas texture management
 *
 * Public API for requesting tiles, checking status, and processing uploads.
 *
 * Design:
 * - Thread-safe RequestTiles (can be called from any thread)
 * - ProcessUploads must be called from GL thread
 * - Automatic state tracking (NotLoaded → Loading → Loaded)
 * - Idempotent tile requests (safe to request same tile multiple times)
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/data/tile_cache.h>
#include <earth_map/data/tile_loader.h>
#include <earth_map/renderer/texture_atlas/tile_load_worker_pool.h>
#include <earth_map/renderer/texture_atlas/gl_upload_queue.h>
#include <earth_map/renderer/tile_pool/tile_texture_pool.h>
#include <earth_map/renderer/tile_pool/indirection_texture_manager.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <cstdint>
#include <chrono>

namespace earth_map {

/**
 * @brief Tile texture coordinator - main public API
 *
 * Orchestrates the complete tile texture pipeline:
 * 1. User requests tiles via RequestTiles()
 * 2. Worker threads load and decode tiles
 * 3. Decoded data pushed to GL upload queue
 * 4. GL thread processes uploads via ProcessUploads()
 * 5. Tiles uploaded to texture atlas
 * 6. User queries UV coordinates via GetTileUV()
 *
 * Thread Safety:
 * - RequestTiles, IsTileReady, GetTileUV: Safe from any thread
 * - ProcessUploads: Must be called from GL thread only
 *
 * Design Rationale:
 * - Separates CPU work (loading/decoding) from GPU work (upload)
 * - State tracking prevents redundant loads
 * - Idempotent requests simplify caller logic
 * - Frame budget control via max_uploads_per_frame
 */
class TileTextureCoordinator {
public:
    static constexpr std::uint32_t kDefaultTileSize = 256;
    static constexpr std::uint32_t kDefaultMaxPoolLayers = 512;

    /**
     * @brief Tile loading state
     */
    enum class TileStatus {
        NotLoaded,  ///< Tile has not been requested
        Loading,    ///< Tile is being loaded/decoded by workers
        Loaded      ///< Tile is in atlas and ready for rendering
    };

    /**
     * @brief Tile state metadata
     */
    struct TileState {
        TileStatus status = TileStatus::NotLoaded;
        int pool_layer = -1;  ///< Tile pool layer index (valid if Loaded)
        std::chrono::steady_clock::time_point request_time;  ///< When tile was requested
    };

    /**
     * @brief Constructor
     *
     * @param cache Tile cache (may be null)
     * @param loader Tile loader (required)
     * @param num_worker_threads Number of worker threads (default: 4)
     * @param skip_gl_init Skip OpenGL initialization for testing (default: false)
     */
    explicit TileTextureCoordinator(
        std::shared_ptr<TileCache> cache,
        std::shared_ptr<TileLoader> loader,
        int num_worker_threads = 4,
        bool skip_gl_init = false);

    /**
     * @brief Destructor
     */
    ~TileTextureCoordinator();

    // Non-copyable
    TileTextureCoordinator(const TileTextureCoordinator&) = delete;
    TileTextureCoordinator& operator=(const TileTextureCoordinator&) = delete;

    // Non-movable
    TileTextureCoordinator(TileTextureCoordinator&&) = delete;
    TileTextureCoordinator& operator=(TileTextureCoordinator&&) = delete;

    /**
     * @brief Request tiles to load (non-blocking, idempotent)
     *
     * Submits tile load requests to worker pool. Tiles already loaded or
     * currently loading are skipped (idempotent behavior).
     *
     * @param tiles List of tile coordinates to load
     * @param priority Priority (lower number = higher priority, default: 0)
     *
     * Thread Safety: Safe to call from any thread
     * Performance: O(n) where n = tiles.size()
     */
    void RequestTiles(
        const std::vector<TileCoordinates>& tiles,
        int priority = 0);

    /**
     * @brief Check if tile is ready for rendering
     *
     * Returns true if tile is loaded in atlas and ready to render.
     *
     * @param coords Tile coordinates
     * @return true if tile is in atlas, false otherwise
     *
     * Thread Safety: Safe to call from any thread
     * Performance: O(1)
     */
    bool IsTileReady(const TileCoordinates& coords) const;

    /**
     * @brief Get UV coordinates for tile
     *
     * Returns UV coordinates for rendering the tile from the atlas.
     * If tile is not loaded, returns default UV (0, 0, 0, 0).
     *
     * @param coords Tile coordinates
     * @return UV coordinates (u_min, v_min, u_max, v_max)
     *
     * Thread Safety: Safe to call from any thread
     * Performance: O(1)
     */
    glm::vec4 GetTileUV(const TileCoordinates& coords) const;

    /**
     * @brief Get tile pool texture array ID
     *
     * Returns the OpenGL GL_TEXTURE_2D_ARRAY ID for the tile pool.
     *
     * @return OpenGL texture ID
     */
    std::uint32_t GetTilePoolTextureID() const;

    /**
     * @brief Get indirection texture ID for a zoom level
     *
     * @return OpenGL texture ID, or 0 if not allocated
     */
    std::uint32_t GetIndirectionTextureID(int zoom) const;

    /**
     * @brief Get indirection window offset for a zoom level
     *
     * For full-mode zooms (0-12), returns (0,0).
     * For windowed zooms (13+), returns the tile coordinate offset.
     */
    glm::ivec2 GetIndirectionOffset(int zoom) const;

    /**
     * @brief Update indirection window center for windowed zoom levels
     *
     * Should be called when camera moves to keep the indirection window
     * centered on the visible area.
     */
    void UpdateIndirectionWindowCenter(int zoom, int center_tile_x, int center_tile_y);

    /**
     * @brief Get tile pool layer index for a tile
     *
     * @return Layer index, or -1 if not loaded
     */
    int GetTileLayerIndex(const TileCoordinates& coords) const;

    /**
     * @brief Get atlas texture ID (legacy alias for GetTilePoolTextureID)
     */
    std::uint32_t GetAtlasTextureID() const;

    /**
     * @brief Process upload queue (must be called from GL thread)
     *
     * Drains the GL upload queue and uploads tiles to the atlas.
     * Should be called once per frame from the rendering thread.
     *
     * @param max_uploads_per_frame Maximum uploads per call (default: 5)
     *                              Limits GPU upload time per frame
     *
     * Thread Safety: MUST be called from GL thread only
     * Performance: O(max_uploads_per_frame)
     */
    void ProcessUploads(int max_uploads_per_frame = 5);

    /**
     * @brief Evict tiles not used recently
     *
     * Removes tiles from atlas that haven't been accessed for a given duration.
     * Frees atlas slots for new tiles.
     *
     * @param max_age Maximum age before eviction (default: 5 minutes)
     * @return Number of tiles evicted
     *
     * Thread Safety: Must be called from GL thread (modifies atlas)
     */
    std::size_t EvictUnusedTiles(
        std::chrono::seconds max_age = std::chrono::seconds(300));

    /**
     * @brief Get tile status
     *
     * @param coords Tile coordinates
     * @return Tile status (NotLoaded, Loading, or Loaded)
     *
     * Thread Safety: Safe to call from any thread
     */
    TileStatus GetTileStatus(const TileCoordinates& coords) const;

    /**
     * @brief Get number of tiles currently in Loading state
     *
     * Thread Safety: Safe to call from any thread (atomic)
     */
    std::size_t GetPendingLoadCount() const { return pending_load_count_.load(); }

    /// Maximum number of concurrent pending tile loads before backpressure kicks in
    static constexpr std::size_t kMaxPendingLoads = 256;

private:
    /**
     * @brief Callback when worker completes tile loading
     *
     * Called by worker threads after tile is loaded and queued for upload.
     * Marks tile as ready to transition to Loaded state.
     *
     * @param coords Tile coordinates
     */
    void OnTileLoadComplete(const TileCoordinates& coords);

    /// Tile state map (coordinates → state)
    std::unordered_map<TileCoordinates, TileState, TileCoordinatesHash> tile_states_;

    /// Mutex protecting tile_states_ (read-write lock for concurrency)
    mutable std::shared_mutex state_mutex_;

    /// Worker pool for loading and decoding tiles
    std::unique_ptr<TileLoadWorkerPool> worker_pool_;

    /// GL upload queue (MPSC queue)
    std::shared_ptr<GLUploadQueue> upload_queue_;

    /// Tile texture pool (GL_TEXTURE_2D_ARRAY, GL thread only)
    std::unique_ptr<TileTexturePool> tile_pool_;

    /// Indirection texture manager (per-zoom lookup textures, GL thread only)
    std::unique_ptr<IndirectionTextureManager> indirection_manager_;

    /// Number of tiles currently in Loading state (atomic for lock-free reads)
    std::atomic<std::size_t> pending_load_count_{0};
};

} // namespace earth_map
