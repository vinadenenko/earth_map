#pragma once

/**
 * @file tile_manager.h
 * @brief Tile management system for globe rendering
 * 
 * Provides tile management with coordinate systems, indexing, and caching
 * for efficient map tile handling in the globe rendering system.
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/math/bounding_box.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>

namespace earth_map {

// Forward declarations
class TileCache;

/**
 * @brief Tile data structure
 */
struct Tile {
    /** Tile coordinates */
    TileCoordinates coordinates;
    
    /** Tile bounds in geographic coordinates */
    BoundingBox2D geographic_bounds;
    
    /** Tile level of detail */
    std::uint8_t lod_level;
    
    /** Tile priority for loading */
    float priority = 0.0f;
    
    /** Whether tile is currently loaded */
    bool loaded = false;
    
    /** Whether tile is currently visible */
    bool visible = false;
    
    /** Tile age for cache eviction */
    std::uint32_t age = 0;
    
    /** Distance from camera for LOD selection */
    float camera_distance = 0.0f;
    
    /** Screen-space error for LOD selection */
    float screen_error = 0.0f;
    
    /**
     * @brief Default constructor
     */
    Tile() = default;
    
    /**
     * @brief Construct from coordinates
     */
    explicit Tile(const TileCoordinates& coords) 
        : coordinates(coords), lod_level(coords.zoom) {}
};

/**
 * @brief Tile manager configuration
 */
struct TileManagerConfig {
    /** Maximum number of tiles to keep in memory */
    std::size_t max_tiles_in_memory = 1000;
    
    /** Maximum number of tiles to load per frame */
    std::size_t max_tiles_per_frame = 10;
    
    /** Cache eviction strategy */
    enum class EvictionStrategy {
        LRU,    ///< Least Recently Used
        PRIORITY, ///< Priority-based eviction
        DISTANCE ///< Distance-based eviction
    } eviction_strategy = EvictionStrategy::LRU;
    
    /** Default LOD level */
    std::uint8_t default_lod_level = 10;
    
    /** Maximum LOD level */
    std::uint8_t max_lod_level = 18;
    
    /** Minimum LOD level */
    std::uint8_t min_lod_level = 0;
    
    /** Enable automatic LOD selection */
    bool enable_auto_lod = true;
    
    /** Maximum screen-space error in pixels */
    float max_screen_error = 2.0f;
    
    /** Distance bias for LOD selection */
    float lod_distance_bias = 1.0f;
};

/**
 * @brief Tile manager interface
 * 
 * Manages tile loading, caching, and LOD selection for globe rendering.
 * Provides efficient tile access and resource management.
 */
class TileManager {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~TileManager() = default;
    
    /**
     * @brief Initialize the tile manager
     * 
     * @param config Configuration parameters
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize(const TileManagerConfig& config) = 0;
    
    /**
     * @brief Update tile manager (called once per frame)
     * 
     * @param camera_position Current camera position in world coordinates
     * @param view_matrix Current view matrix
     * @param projection_matrix Current projection matrix
     * @param viewport_size Current viewport size
     * @return true if update succeeded, false otherwise
     */
    virtual bool Update(const glm::vec3& camera_position,
                    const glm::mat4& view_matrix,
                    const glm::mat4& projection_matrix,
                    const glm::vec2& viewport_size) = 0;
    
    /**
     * @brief Get visible tiles for rendering
     * 
     * @return std::vector<const Tile*> List of visible tiles
     */
    virtual std::vector<const Tile*> GetVisibleTiles() const = 0;
    
    /**
     * @brief Get tile by coordinates
     * 
     * @param coordinates Tile coordinates
     * @return const Tile* Tile pointer or nullptr if not found
     */
    virtual const Tile* GetTile(const TileCoordinates& coordinates) const = 0;
    
    /**
     * @brief Get OpenGL texture ID for tile
     * 
     * @param coordinates Tile coordinates
     * @return std::uint32_t OpenGL texture ID or 0 if not found
     */
    virtual std::uint32_t GetTileTexture(const TileCoordinates& coordinates) const = 0;
    
    /**
     * @brief Load tile by coordinates
     * 
     * @param coordinates Tile coordinates to load
     * @return true if loading started successfully, false otherwise
     */
    virtual bool LoadTile(const TileCoordinates& coordinates) = 0;
    
    /**
     * @brief Unload tile by coordinates
     * 
     * @param coordinates Tile coordinates to unload
     * @return true if unloading succeeded, false otherwise
     */
    virtual bool UnloadTile(const TileCoordinates& coordinates) = 0;
    
    /**
     * @brief Get tiles in geographic bounds
     * 
     * @param bounds Geographic bounds to query
     * @return std::vector<const Tile*> List of tiles in bounds
     */
    virtual std::vector<const Tile*> GetTilesInBounds(
        const BoundingBox2D& bounds) const = 0;
    
    /**
     * @brief Get tiles at specific LOD level
     * 
     * @param lod_level LOD level to query
     * @return std::vector<const Tile*> List of tiles at LOD level
     */
    virtual std::vector<const Tile*> GetTilesAtLOD(std::uint8_t lod_level) const = 0;
    
    /**
     * @brief Calculate optimal LOD level for screen area
     * 
     * @param geographic_bounds Geographic bounds to cover
     * @param viewport_size Current viewport size
     * @param camera_distance Distance from camera to area
     * @return std::uint8_t Optimal LOD level
     */
    virtual std::uint8_t CalculateOptimalLOD(
        const BoundingBox2D& geographic_bounds,
        const glm::vec2& viewport_size,
        float camera_distance) const = 0;
    
    /**
     * @brief Get tile statistics
     * 
     * @return std::pair<std::size_t, std::size_t> (loaded_tiles, visible_tiles)
     */
    virtual std::pair<std::size_t, std::size_t> GetStatistics() const = 0;
    
    /**
     * @brief Clear all tiles
     */
    virtual void Clear() = 0;
    
    /**
     * @brief Get configuration
     * 
     * @return TileManagerConfig Current configuration
     */
    virtual TileManagerConfig GetConfiguration() const = 0;
    
    /**
     * @brief Set configuration
     * 
     * @param config New configuration parameters
     * @return true if configuration was applied, false otherwise
     */
    virtual bool SetConfiguration(const TileManagerConfig& config) = 0;

protected:
    /**
     * @brief Protected constructor
     */
    TileManager() = default;
};

/**
 * @brief Factory function to create tile manager
 * 
 * @param config Configuration parameters
 * @return std::unique_ptr<TileManager> New tile manager instance
 */
std::unique_ptr<TileManager> CreateTileManager(const TileManagerConfig& config = {});

/**
 * @brief Basic tile manager implementation
 * 
 * Implements tile management with LOD selection, caching, and
 * efficient resource management for globe rendering.
 */
class BasicTileManager : public TileManager {
public:
    explicit BasicTileManager(const TileManagerConfig& config = {});
    ~BasicTileManager() override = default;
    
    bool Initialize(const TileManagerConfig& config) override;
    bool Update(const glm::vec3& camera_position,
               const glm::mat4& view_matrix,
               const glm::mat4& projection_matrix,
               const glm::vec2& viewport_size) override;
    
    std::vector<const Tile*> GetVisibleTiles() const override;
    const Tile* GetTile(const TileCoordinates& coordinates) const override;
    std::uint32_t GetTileTexture(const TileCoordinates& coordinates) const override;
    bool LoadTile(const TileCoordinates& coordinates) override;
    bool UnloadTile(const TileCoordinates& coordinates) override;
    
    std::vector<const Tile*> GetTilesInBounds(const BoundingBox2D& bounds) const override;
    std::vector<const Tile*> GetTilesAtLOD(std::uint8_t lod_level) const override;
    
    std::uint8_t CalculateOptimalLOD(
        const BoundingBox2D& geographic_bounds,
        const glm::vec2& viewport_size,
        float camera_distance) const override;
    
    std::pair<std::size_t, std::size_t> GetStatistics() const override;
    void Clear() override;
    
    TileManagerConfig GetConfiguration() const override;
    bool SetConfiguration(const TileManagerConfig& config) override;

private:
    TileManagerConfig config_;
    std::vector<std::unique_ptr<Tile>> tiles_;
    std::vector<const Tile*> visible_tiles_;
    glm::vec3 last_camera_position_ = glm::vec3(0.0f);
    glm::vec2 last_viewport_size_ = glm::vec2(0.0f);
    bool needs_update_ = true;
    
    /**
     * @brief Find or create tile by coordinates
     */
    Tile* FindOrCreateTile(const TileCoordinates& coordinates);
    
    /**
     * @brief Update tile visibility based on camera position
     */
    void UpdateVisibility(const glm::vec3& camera_position,
                      const glm::mat4& view_matrix,
                      const glm::mat4& projection_matrix,
                      const glm::vec2& viewport_size);
    
    /**
     * @brief Perform cache eviction if needed
     */
    void EvictTiles();
    
    /**
     * @brief Calculate tile priority for loading
     */
    float CalculateTilePriority(const Tile& tile,
                          const glm::vec3& camera_position) const;
    
    /**
     * @brief Get tiles sorted by priority
     */
    std::vector<const Tile*> GetTilesByPriority() const;
    
    /**
     * @brief Calculate screen-space error for a tile
     */
    float CalculateScreenSpaceError(const TileCoordinates& tile_coords,
                                 const glm::vec2& viewport_size,
                                 float camera_distance) const;
    
    /**
     * @brief Calculate maximum visible distance for LOD level
     */
    float CalculateMaxVisibleDistance(std::uint8_t lod_level) const;
};

} // namespace earth_map
