#pragma once

/**
 * @file tile_texture_manager.h
 * @brief Tile texture management system for dynamic globe texturing
 * 
 * Manages OpenGL texture objects for tiles, handles texture creation,
 * updates, and lifecycle management. Supports seamless tile transitions
 * and efficient texture memory management for globe rendering.
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/data/tile_cache.h>
#include <earth_map/data/tile_loader.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <unordered_map>
#include <memory>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <functional>

namespace earth_map {

/**
 * @brief Texture tile information
 */
struct TextureTile {
    /** OpenGL texture ID */
    std::uint32_t texture_id = 0;
    
    /** Tile coordinates */
    TileCoordinates coordinates;
    
    /** Texture dimensions */
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    
    /** Number of channels */
    std::uint8_t channels = 0;
    
    /** Whether texture is currently bound */
    bool is_bound = false;
    
    /** Last used timestamp */
    std::chrono::steady_clock::time_point last_used;
    
    /** Generation counter for tracking updates */
    std::uint32_t generation = 0;
    
    /** Whether texture has valid data */
    bool is_valid = false;
    
    /**
     * @brief Default constructor
     */
    TextureTile() = default;
    
    /**
     * @brief Constructor with coordinates
     */
    explicit TextureTile(const TileCoordinates& coords) 
        : coordinates(coords), last_used(std::chrono::steady_clock::now()) {}
};

/**
 * @brief Texture atlas information
 */
struct TextureAtlas {
    /** Atlas texture ID */
    std::uint32_t atlas_id = 0;
    
    /** Atlas dimensions */
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    
    /** Tile size within atlas */
    std::uint32_t tile_size = 256;
    
    /** Grid dimensions (tiles per row/column) */
    std::uint32_t grid_width = 0;
    std::uint32_t grid_height = 0;
    
    /** Occupancy map for atlas slots */
    std::vector<bool> occupied;
    
    /** Free slot indices */
    std::vector<std::size_t> free_slots;
    
    /** Tile to atlas slot mapping */
    std::unordered_map<TileCoordinates, std::size_t, TileCoordinatesHash> tile_slots;
    
    /**
     * @brief Get atlas UV coordinates for a tile
     * 
     * @param slot Slot index in atlas
     * @return glm::vec4 UV coordinates (x1, y1, x2, y2)
     */
    glm::vec4 GetTileUV(std::size_t slot) const;
    
    /**
     * @brief Get atlas UV coordinates for tile coordinates
     * 
     * @param coords Tile coordinates
     * @return glm::vec4 UV coordinates (x1, y1, x2, y2)
     */
    glm::vec4 GetTileUV(const TileCoordinates& coords) const;
};

/**
 * @brief Tile texture manager configuration
 */
struct TileTextureManagerConfig {
    /** Maximum number of textures in memory */
    std::size_t max_textures = 1000;
    
    /** Maximum texture memory usage in MB */
    std::size_t max_texture_memory_mb = 512;
    
    /** Tile texture size (assumed square) */
    std::uint32_t tile_size = 256;
    
    /** Use texture atlases for small tiles */
    bool use_texture_atlas = true;
    
    /** Atlas size (multiple of tile_size) */
    std::uint32_t atlas_size = 2048;
    
    /** Texture filtering mode */
    enum class FilterMode {
        NEAREST,     ///< Nearest neighbor filtering
        LINEAR,      ///< Linear filtering
        MIPMAP       ///< Use mipmaps
    } filter_mode = FilterMode::LINEAR;
    
    /** Texture wrapping mode */
    enum class WrapMode {
        CLAMP,      ///< Clamp to edge
        REPEAT       ///< Repeat texture
    } wrap_mode = WrapMode::CLAMP;
    
    /** Texture format */
    enum class TextureFormat {
        RGB,        ///< 3-channel RGB
        RGBA,       ///< 4-channel RGBA
        COMPRESSED   ///< Compressed texture format
    } texture_format = TextureFormat::RGB;
    
    /** Anisotropic filtering level (0 = disabled) */
    std::uint32_t anisotropic_level = 4;
    
    /** Enable texture compression */
    bool enable_compression = false;
    
    /** Preload adjacent tiles */
    bool preload_adjacent = true;
    
    /** Eviction timeout in seconds */
    std::uint32_t eviction_timeout_seconds = 300;
};

/**
 * @brief Tile texture manager statistics
 */
struct TileTextureManagerStats {
    /** Total textures loaded */
    std::size_t total_textures = 0;
    
    /** Texture memory usage in bytes */
    std::size_t texture_memory_bytes = 0;
    
    /** Number of texture atlases */
    std::size_t atlas_count = 0;
    
    /** Atlas memory usage in bytes */
    std::size_t atlas_memory_bytes = 0;
    
    /** Number of tiles in atlases */
    std::size_t atlas_tiles = 0;
    
    /** Cache hit rate */
    float cache_hit_rate = 0.0f;
    
    /** Number of texture uploads per frame */
    std::uint32_t uploads_per_frame = 0;
    
    /** Number of texture evictions */
    std::uint32_t evictions_count = 0;
    
    /** Maximum texture memory usage reached */
    std::size_t peak_memory_bytes = 0;
    
    /**
     * @brief Get texture memory usage in MB
     */
    float GetTextureMemoryMB() const {
        return static_cast<float>(texture_memory_bytes) / (1024.0f * 1024.0f);
    }
    
    /**
     * @brief Get atlas memory usage in MB
     */
    float GetAtlasMemoryMB() const {
        return static_cast<float>(atlas_memory_bytes) / (1024.0f * 1024.0f);
    }
    
    /**
     * @brief Get total memory usage in MB
     */
    float GetTotalMemoryMB() const {
        return GetTextureMemoryMB() + GetAtlasMemoryMB();
    }
    
    /**
     * @brief Reset statistics
     */
    void Reset() {
        total_textures = 0;
        texture_memory_bytes = 0;
        atlas_count = 0;
        atlas_memory_bytes = 0;
        atlas_tiles = 0;
        cache_hit_rate = 0.0f;
        uploads_per_frame = 0;
        evictions_count = 0;
    }
};

/**
 * @brief Tile texture loading callback
 */
using TileTextureCallback = std::function<void(const TileCoordinates&, std::uint32_t texture_id)>;

/**
 * @brief Tile texture manager interface
 * 
 * Manages OpenGL textures for map tiles, handles texture creation,
 * updates, and efficient memory management.
 */
class TileTextureManager {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~TileTextureManager() = default;
    
    /**
     * @brief Initialize the texture manager
     * 
     * @param config Configuration parameters
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize(const TileTextureManagerConfig& config) = 0;
    
    /**
     * @brief Set tile cache
     * 
     * @param cache Tile cache instance
     */
    virtual void SetTileCache(std::shared_ptr<TileCache> cache) = 0;
    
    /**
     * @brief Set tile loader
     * 
     * @param loader Tile loader instance
     */
    virtual void SetTileLoader(std::shared_ptr<TileLoader> loader) = 0;
    
    /**
     * @brief Get or create texture for tile
     * 
     * @param coordinates Tile coordinates
     * @return std::uint32_t OpenGL texture ID (0 if not available)
     */
    virtual std::uint32_t GetTexture(const TileCoordinates& coordinates) = 0;
    
    /**
     * @brief Load tile texture asynchronously
     * 
     * @param coordinates Tile coordinates
     * @param callback Completion callback (optional)
     * @return std::future<bool> Future indicating success
     */
    virtual std::future<bool> LoadTextureAsync(
        const TileCoordinates& coordinates,
        TileTextureCallback callback = nullptr) = 0;
    
    /**
     * @brief Load multiple tile textures asynchronously
     * 
     * @param coordinates List of tile coordinates
     * @param callback Completion callback for each tile (optional)
     * @return std::vector<std::future<bool>> Futures for each load operation
     */
    virtual std::vector<std::future<bool>> LoadTexturesAsync(
        const std::vector<TileCoordinates>& coordinates,
        TileTextureCallback callback = nullptr) = 0;
    
    /**
     * @brief Update texture with new tile data
     * 
     * @param coordinates Tile coordinates
     * @param tile_data Tile data containing image bytes
     * @return true if update succeeded, false otherwise
     */
    virtual bool UpdateTexture(const TileCoordinates& coordinates,
                             const TileData& tile_data) = 0;
    
    /**
     * @brief Bind texture for rendering
     * 
     * @param texture_id OpenGL texture ID
     * @param texture_unit OpenGL texture unit (default: 0)
     */
    virtual void BindTexture(std::uint32_t texture_id, 
                           std::uint32_t texture_unit = 0) = 0;
    
    /**
     * @brief Get UV coordinates for tile in atlas
     * 
     * @param coordinates Tile coordinates
     * @return glm::vec4 UV coordinates (x1, y1, x2, y2)
     */
    virtual glm::vec4 GetTileUV(const TileCoordinates& coordinates) = 0;
    
    /**
     * @brief Check if tile texture is available
     * 
     * @param coordinates Tile coordinates
     * @return true if texture is available, false otherwise
     */
    virtual bool IsTextureAvailable(const TileCoordinates& coordinates) = 0;
    
    /**
     * @brief Preload textures for visible tiles
     * 
     * @param visible_tiles List of visible tile coordinates
     * @return std::size_t Number of textures preloaded
     */
    virtual std::size_t PreloadVisibleTiles(
        const std::vector<TileCoordinates>& visible_tiles) = 0;
    
    /**
     * @brief Evict unused textures
     * 
     * @param force_eviction Force eviction even of recently used textures
     * @return std::size_t Number of textures evicted
     */
    virtual std::size_t EvictUnusedTextures(bool force_eviction = false) = 0;
    
    /**
     * @brief Get texture manager statistics
     * 
     * @return TileTextureManagerStats Current statistics
     */
    virtual TileTextureManagerStats GetStatistics() const = 0;
    
    /**
     * @brief Get configuration
     * 
     * @return TileTextureManagerConfig Current configuration
     */
    virtual TileTextureManagerConfig GetConfiguration() const = 0;
    
    /**
     * @brief Set configuration
     * 
     * @param config New configuration
     * @return true if configuration was applied, false otherwise
     */
    virtual bool SetConfiguration(const TileTextureManagerConfig& config) = 0;
    
    /**
     * @begin Update per-frame statistics and cleanup
     */
    virtual void Update() = 0;
    
    /**
     * @brief Clean up all textures
     */
    virtual void Cleanup() = 0;

protected:
    /**
     * @brief Protected constructor
     */
    TileTextureManager() = default;
};

/**
 * @brief Factory function to create tile texture manager
 * 
 * @param config Configuration parameters
 * @return std::unique_ptr<TileTextureManager> New texture manager instance
 */
std::unique_ptr<TileTextureManager> CreateTileTextureManager(
    const TileTextureManagerConfig& config = {});

} // namespace earth_map