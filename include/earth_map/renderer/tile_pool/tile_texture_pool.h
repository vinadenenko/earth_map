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

#include <earth_map/math/tile_mathematics.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

namespace earth_map {

/**
 * @brief Tile texture pool using GL_TEXTURE_2D_ARRAY
 *
 * Each tile occupies one layer in the array. Layers are managed with LRU
 * eviction. The shader samples via texture(sampler2DArray, vec3(u, v, layer)).
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
     * If the tile already exists, updates in place. If the pool is full,
     * evicts the LRU tile.
     *
     * @return Layer index (0 to max_layers-1), or -1 on failure
     */
    int UploadTile(
        const TileCoordinates& coords,
        const std::uint8_t* pixel_data,
        std::uint32_t width,
        std::uint32_t height,
        std::uint8_t channels);

    /**
     * @brief Evict a tile from the pool
     *
     * Frees the layer for reuse. No-op if tile is not in pool.
     */
    void EvictTile(const TileCoordinates& coords);

    /**
     * @brief Check if a tile is loaded in the pool
     */
    bool IsTileLoaded(const TileCoordinates& coords) const;

    /**
     * @brief Get the layer index for a tile
     *
     * @return Layer index, or -1 if not loaded
     */
    int GetLayerIndex(const TileCoordinates& coords) const;

    /**
     * @brief Update LRU timestamp for a tile without re-uploading
     *
     * Call this when a tile is accessed/rendered to prevent eviction.
     */
    void TouchTile(const TileCoordinates& coords);

    /** @brief Get OpenGL texture array ID (0 if GL not initialized) */
    std::uint32_t GetTextureArrayID() const { return texture_array_id_; }

    /** @brief Get maximum number of layers */
    std::uint32_t GetMaxLayers() const { return max_layers_; }

    /** @brief Get tile size in pixels */
    std::uint32_t GetTileSize() const { return tile_size_; }

    /** @brief Get number of occupied layers */
    std::size_t GetOccupiedLayers() const { return coord_to_layer_.size(); }

    /** @brief Get number of free layers */
    std::size_t GetFreeLayers() const { return free_layers_.size(); }

private:
    struct LayerSlot {
        TileCoordinates coords;
        bool occupied = false;
        std::chrono::steady_clock::time_point last_used;
        int layer_index = -1;

        LayerSlot() = default;
        explicit LayerSlot(int index)
            : last_used(std::chrono::steady_clock::now()), layer_index(index) {}
    };

    void CreateTextureArray();
    int AllocateLayer();
    int FindEvictionCandidate() const;
    void FreeLayer(int layer_index);

    std::uint32_t texture_array_id_ = 0;
    std::uint32_t tile_size_;
    std::uint32_t max_layers_;
    bool skip_gl_init_;

    std::vector<LayerSlot> layers_;
    std::queue<int> free_layers_;
    std::unordered_map<TileCoordinates, int, TileCoordinatesHash> coord_to_layer_;
};

} // namespace earth_map
