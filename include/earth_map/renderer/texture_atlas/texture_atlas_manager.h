#pragma once

/**
 * @file texture_atlas_manager.h
 * @brief OpenGL texture atlas manager for tile rendering
 *
 * Manages a single large OpenGL texture (atlas) that contains multiple tile textures.
 * Provides efficient texture packing, UV coordinate calculation, and LRU eviction.
 *
 * Design:
 * - Atlas size: 2048x2048 pixels
 * - Tile size: 256x256 pixels
 * - Grid: 8x8 = 64 tile slots
 * - Eviction: LRU (Least Recently Used)
 * - Thread safety: Designed for single-threaded GL access only
 */

#include <earth_map/math/tile_mathematics.h>
#include <glm/vec4.hpp>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <queue>
#include <chrono>
#include <memory>

namespace earth_map {

/**
 * @brief Atlas slot metadata
 *
 * Stores information about each slot in the texture atlas.
 */
struct AtlasSlot {
    /// Tile coordinates stored in this slot
    TileCoordinates coords;

    /// Whether slot is currently occupied
    bool occupied = false;

    /// Last access timestamp (for LRU eviction)
    std::chrono::steady_clock::time_point last_used;

    /// Cached UV coordinates for this slot
    glm::vec4 uv_coords;

    /// Slot index in atlas grid
    int slot_index = -1;

    /**
     * @brief Default constructor
     */
    AtlasSlot() = default;

    /**
     * @brief Construct slot with index
     */
    explicit AtlasSlot(int index)
        : last_used(std::chrono::steady_clock::now()), slot_index(index) {}
};

/**
 * @brief Texture atlas manager for tile textures
 *
 * Manages a single large OpenGL texture atlas containing multiple tile textures.
 * Handles slot allocation, UV coordinate calculation, and LRU-based eviction.
 *
 * Thread Safety:
 * - NOT thread-safe - designed for single-threaded GL thread access only
 * - All methods must be called from the OpenGL rendering thread
 *
 * Memory Management:
 * - Fixed-size atlas (2048x2048, 64 slots)
 * - LRU eviction when atlas is full
 * - Automatic slot reuse for duplicate uploads
 *
 * Design Rationale:
 * - Reduces texture bind overhead (single atlas texture)
 * - Efficient GPU memory usage via packing
 * - Simple LRU eviction prevents unbounded memory growth
 * - Fixed grid layout simplifies UV calculation
 */
class TextureAtlasManager {
public:
    /**
     * @brief Constructor
     *
     * @param atlas_width Atlas texture width in pixels (default: 2048)
     * @param atlas_height Atlas texture height in pixels (default: 2048)
     * @param tile_size Tile size in pixels (default: 256)
     * @param skip_gl_init Skip OpenGL initialization (for testing)
     */
    explicit TextureAtlasManager(
        std::uint32_t atlas_width = 2048,
        std::uint32_t atlas_height = 2048,
        std::uint32_t tile_size = 256,
        bool skip_gl_init = false);

    /**
     * @brief Destructor
     *
     * Cleans up OpenGL resources.
     */
    ~TextureAtlasManager();

    // Non-copyable
    TextureAtlasManager(const TextureAtlasManager&) = delete;
    TextureAtlasManager& operator=(const TextureAtlasManager&) = delete;

    // Non-movable (owns GL resources)
    TextureAtlasManager(TextureAtlasManager&&) = delete;
    TextureAtlasManager& operator=(TextureAtlasManager&&) = delete;

    /**
     * @brief Upload tile texture to atlas
     *
     * Allocates a slot in the atlas and uploads the tile's pixel data.
     * If tile is already in atlas, updates in place (same slot).
     * If atlas is full, evicts LRU tile and reuses its slot.
     *
     * @param coords Tile coordinates
     * @param pixel_data Pointer to pixel data (RGB or RGBA)
     * @param width Image width in pixels (should match tile_size)
     * @param height Image height in pixels (should match tile_size)
     * @param channels Number of channels (3 for RGB, 4 for RGBA)
     * @return Slot index (0-63), or -1 on failure
     *
     * Thread Safety: Must be called from GL thread
     */
    int UploadTile(
        const TileCoordinates& coords,
        const std::uint8_t* pixel_data,
        std::uint32_t width,
        std::uint32_t height,
        std::uint8_t channels);

    /**
     * @brief Get UV coordinates for a tile
     *
     * Returns UV coordinates for the tile's location in the atlas.
     * Updates the tile's last-used timestamp (LRU tracking).
     *
     * @param coords Tile coordinates
     * @return UV coordinates (u_min, v_min, u_max, v_max)
     *         Returns (0, 0, 0, 0) if tile not in atlas
     *
     * Thread Safety: Must be called from GL thread
     */
    glm::vec4 GetTileUV(const TileCoordinates& coords);

    /**
     * @brief Calculate UV coordinates for a slot index
     *
     * Pure function - no side effects.
     *
     * @param slot_index Slot index (0-63)
     * @return UV coordinates (u_min, v_min, u_max, v_max)
     */
    glm::vec4 CalculateSlotUV(int slot_index) const;

    /**
     * @brief Get atlas texture ID
     *
     * @return OpenGL texture ID for the atlas
     */
    std::uint32_t GetAtlasTextureID() const {
        return atlas_texture_id_;
    }

    /**
     * @brief Check if tile is loaded in atlas
     *
     * @param coords Tile coordinates
     * @return true if tile is in atlas, false otherwise
     */
    bool IsTileLoaded(const TileCoordinates& coords) const;

    /**
     * @brief Evict tile from atlas
     *
     * Frees the slot occupied by the tile.
     * No-op if tile is not in atlas.
     *
     * @param coords Tile coordinates to evict
     *
     * Thread Safety: Must be called from GL thread
     */
    void EvictTile(const TileCoordinates& coords);

    /**
     * @brief Get number of free slots
     *
     * @return Number of available slots in atlas
     */
    std::size_t GetFreeSlots() const {
        return free_slots_.size();
    }

    /**
     * @brief Get number of occupied slots
     *
     * @return Number of tiles currently in atlas
     */
    std::size_t GetOccupiedSlots() const {
        return coord_to_slot_.size();
    }

    /**
     * @brief Get total number of slots
     *
     * @return Total slots in atlas (grid_width * grid_height)
     */
    std::size_t GetTotalSlots() const {
        return slots_.size();
    }

    /**
     * @brief Get atlas width
     *
     * @return Atlas width in pixels
     */
    std::uint32_t GetAtlasWidth() const {
        return atlas_width_;
    }

    /**
     * @brief Get atlas height
     *
     * @return Atlas height in pixels
     */
    std::uint32_t GetAtlasHeight() const {
        return atlas_height_;
    }

    /**
     * @brief Get tile size
     *
     * @return Tile size in pixels
     */
    std::uint32_t GetTileSize() const {
        return tile_size_;
    }

    /**
     * @brief Get grid width (tiles per row)
     *
     * @return Number of tiles per row in atlas
     */
    std::uint32_t GetGridWidth() const {
        return grid_width_;
    }

    /**
     * @brief Get grid height (tiles per column)
     *
     * @return Number of tiles per column in atlas
     */
    std::uint32_t GetGridHeight() const {
        return grid_height_;
    }

private:
    /**
     * @brief Create OpenGL atlas texture
     *
     * Allocates GPU memory for the atlas texture.
     */
    void CreateAtlasTexture();

    /**
     * @brief Find LRU (Least Recently Used) slot for eviction
     *
     * @return Slot index to evict, or -1 if no occupied slots
     */
    int FindEvictionCandidate() const;

    /**
     * @brief Allocate a free slot
     *
     * Returns index of free slot, or -1 if atlas is full.
     * If full, automatically evicts LRU tile.
     *
     * @return Slot index, or -1 on failure
     */
    int AllocateSlot();

    /**
     * @brief Free a slot
     *
     * Marks slot as free and adds to free list.
     *
     * @param slot_index Slot index to free
     */
    void FreeSlot(int slot_index);

    /// OpenGL atlas texture ID
    std::uint32_t atlas_texture_id_ = 0;

    /// Atlas dimensions
    std::uint32_t atlas_width_;
    std::uint32_t atlas_height_;

    /// Tile size in pixels
    std::uint32_t tile_size_;

    /// Grid dimensions
    std::uint32_t grid_width_;
    std::uint32_t grid_height_;

    /// Skip OpenGL initialization (for testing)
    bool skip_gl_init_;

    /// Slot metadata (one per grid cell)
    std::vector<AtlasSlot> slots_;

    /// Queue of free slot indices
    std::queue<int> free_slots_;

    /// Map: tile coordinates → slot index
    std::unordered_map<TileCoordinates, int, TileCoordinatesHash> coord_to_slot_;
};

} // namespace earth_map
