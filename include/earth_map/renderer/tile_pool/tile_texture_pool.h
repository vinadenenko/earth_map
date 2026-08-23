#pragma once

/**
 * @file tile_texture_pool.h
 * @brief GL_TEXTURE_2D_ARRAY-based tile texture pool with LRU eviction
 *
 * Manages a pool of texture layers in a GL_TEXTURE_2D_ARRAY. Each layer holds
 * one 256x256 tile. Tiles are uploaded to individual layers and evicted via LRU
 * when the pool is full. This replaces the texture atlas approach for tile
 * rendering, eliminating UV bleeding and atlas repacking costs.
 *
 * Design:
 * - Fixed number of layers (configurable, e.g., 512)
 * - Each layer = one tile at full [0,1] UV range
 * - Upload via glTexSubImage3D (per-layer, no impact on other tiles)
 * - LRU eviction when pool is full
 * - Thread safety: GL thread only (same as TextureAtlasManager)
 */

#include <earth_map/imagery/tile_matrix_set.h>
#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace earth_map {

/**
 * @brief Tile texture pool using GL_TEXTURE_2D_ARRAY
 *
 * Each canonical imagery page occupies one layer in the array. Layers are
 * managed with LRU eviction. The shader samples via
 * texture(sampler2DArray, vec3(u, v, layer)).
 *
 * Thread Safety: NOT thread-safe — GL thread only.
 */
class TileTexturePool {
public:
    /**
     * @brief Constructor
     *
     * @param tile_size Tile dimensions in pixels (tiles are square)
     * @param max_layers Maximum number of layers in the texture array
     * It means 'how many tiles can be stored in GPU VRAM before LRU eviction.
     * To calculate GPU VRAM usage: 256×256×4×512 (tile_width * tile_height * channels * max_layers) = 128 MB
     * @param skip_gl_init Skip OpenGL initialization (for testing)
     */
    explicit TileTexturePool(
        std::uint32_t tile_size = 256,
        std::uint32_t max_layers = 512,
        bool skip_gl_init = false);

    ~TileTexturePool();

    TileTexturePool(const TileTexturePool&) = delete;
    TileTexturePool& operator=(const TileTexturePool&) = delete;
    TileTexturePool(TileTexturePool&&) = delete;
    TileTexturePool& operator=(TileTexturePool&&) = delete;

    /**
     * @brief Upload tile pixels to a layer
     *
     * If the imagery page already exists, updates it in place. If the pool is
     * full, returns failure; the residency coordinator selects and evicts an
     * LRU page explicitly so it can clear the matching page-table entry.
     *
     * @return Layer index (0 to max_layers-1), or -1 on failure
     */
    int UploadTile(
        const imagery::ImageTileKey& imagery_key,
        const std::uint8_t* pixel_data,
        std::uint32_t width,
        std::uint32_t height,
        std::uint8_t channels);

    /**
     * @brief Evict an imagery page from the pool
     *
     * Frees the layer for reuse. No-op if tile is not in pool.
     */
    void EvictTile(const imagery::ImageTileKey& imagery_key);

    /**
     * @brief Check if an imagery page is loaded in the pool
     */
    bool IsTileLoaded(const imagery::ImageTileKey& imagery_key) const;

    /**
     * @brief Get the layer index for an imagery page
     *
     * @return Layer index, or -1 if not loaded
     */
    int GetLayerIndex(const imagery::ImageTileKey& imagery_key) const;

    /**
     * @brief Update an imagery page's LRU timestamp without re-uploading
     *
     * Call this when a tile is accessed/rendered to prevent eviction.
     */
    void TouchTile(const imagery::ImageTileKey& imagery_key);

    /** @brief Get OpenGL texture array ID (0 if GL not initialized) */
    std::uint32_t GetTextureArrayID() const { return texture_array_id_; }

    /** @brief Get maximum number of layers */
    std::uint32_t GetMaxLayers() const { return max_layers_; }

    /** @brief Get tile size in pixels */
    std::uint32_t GetTileSize() const { return tile_size_; }

    /** @brief Get number of occupied layers */
    std::size_t GetOccupiedLayers() const { return imagery_key_to_layer_.size(); }

    /** @brief Get number of free layers */
    std::size_t GetFreeLayers() const { return free_layers_.size(); }

    /**
     * @brief Get the last-used timestamp for an imagery page
     *
     * @return Time point of last access, or time_point::min() if not loaded
     */
    std::chrono::steady_clock::time_point GetLastUsedTime(
        const imagery::ImageTileKey& imagery_key) const;

    /**
     * @brief Record that a draw call sampling this pool's texture array
     * has just been submitted.
     *
     * Writing a new tile into this array (UploadTile()) while the GPU may
     * still be reading it from a prior draw forces the driver into an
     * expensive implicit stall or whole-array copy -- OpenGL only tracks
     * hazards per *object*, not per layer, so any write anywhere in the
     * array conflicts with any in-flight read anywhere in it (see
     * dev_docs/solved-dev-issues/issue-01-tile-upload-gpu-stall.md).
     * IsSafeToUpload() reports once the GPU has confirmed this read is
     * actually finished.
     *
     * Call this once per frame, immediately after the draw call that binds
     * and samples GetTextureArrayID().
     */
    void MarkSampled();

    /**
     * @brief Whether UploadTile() can currently be called without risking
     * the read/write hazard MarkSampled() guards against.
     *
     * Non-blocking (a single, cheap fence-status poll) -- always true if
     * MarkSampled() has never been called (nothing to wait for yet, e.g.
     * before the first frame) or GL is disabled (skip_gl_init).
     */
    bool IsSafeToUpload() const;

    /**
     * @brief Get the LRU eviction candidate
     *
     * Returns the canonical identity of the least-recently-used imagery page.
     * Does NOT evict — the caller must call EvictTile() explicitly.
     *
     * @return Imagery key, or nullopt if pool is empty
     */
    std::optional<imagery::ImageTileKey> GetEvictionCandidate() const;

private:
    struct LayerSlot {
        imagery::ImageTileKey imagery_key;
        bool occupied = false;
        std::chrono::steady_clock::time_point last_used;
        int layer_index = -1;
        /// Iterator into lru_order_ for O(1) splice/erase. Valid only when occupied.
        std::list<int>::iterator lru_it;

        LayerSlot() = default;
        explicit LayerSlot(int index)
            : last_used(std::chrono::steady_clock::now()), layer_index(index) {}
    };

    void CreateTextureArray();
    int AllocateLayer();
    void FreeLayer(int layer_index);

    std::uint32_t texture_array_id_ = 0;
    std::uint32_t tile_size_;
    std::uint32_t max_layers_;
    bool skip_gl_init_;

    /// Opaque GLsync fence set by MarkSampled() (see its doc comment).
    /// Stored as void* rather than GLsync so this public header doesn't
    /// need to include a GL header, the same reason texture_array_id_
    /// above is std::uint32_t rather than GLuint.
    void* sampled_fence_ = nullptr;

    std::vector<LayerSlot> layers_;
    std::queue<int> free_layers_;
    std::unordered_map<imagery::ImageTileKey, int, imagery::ImageTileKeyHash>
        imagery_key_to_layer_;

    /// LRU order: front = most recently used, back = eviction candidate
    std::list<int> lru_order_;
};

} // namespace earth_map
