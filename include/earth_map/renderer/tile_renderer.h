#pragma once

/**
 * @file tile_renderer.h
 * @brief Tile rendering system for globe
 * 
 * Defines the tile renderer interface and structures for rendering
 * map tiles onto a 3D globe mesh with proper LOD management.
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/data/tile_manager.h>
#include <earth_map/math/frustum.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace earth_map {

// Forward declarations
class TileManager;
class TileTextureCoordinator;
class GlobeMesh;
struct Frustum;

/**
 * @brief Tile rendering statistics
 */
struct TileRenderStats {
    /** Number of tiles currently visible */
    std::size_t visible_tiles = 0;
    
    /** Number of tiles successfully rendered */
    std::size_t rendered_tiles = 0;
    
    /** Number of tiles loaded this frame */
    std::size_t tiles_loaded_this_frame = 0;
    
    /** Number of texture binds this frame */
    std::size_t texture_binds = 0;
    
    /** Average LOD level of visible tiles */
    float average_lod = 0.0f;
    
    /** Tile rendering time in milliseconds */
    float render_time_ms = 0.0f;
};

/**
 * @brief Tile rendering configuration
 */
struct TileRenderConfig {
    std::uint32_t max_visible_tiles = 1000;     ///< Maximum tiles to render simultaneously
    float tile_fade_distance = 2.0f;           ///< Distance for tile fade in/out
    bool enable_lod_transitions = true;           ///< Enable smooth LOD transitions
    float min_lod_distance = 100.0f;          ///< Minimum distance for LOD switching
    float max_lod_distance = 10000.0f;         ///< Maximum distance for LOD switching
};

/**
 * @brief Tile renderer interface
 * 
 * Handles rendering of map tiles onto the globe surface with
 * proper texture management, LOD control, and performance optimization.
 */
class TileRenderer {
public:
    /**
     * @brief Create a new tile renderer instance
     * 
     * @param config Rendering configuration
     * @return std::unique_ptr<TileRenderer> New tile renderer instance
     */
    static std::unique_ptr<TileRenderer> Create(const TileRenderConfig& config = TileRenderConfig{});
    
    /**
     * @brief Virtual destructor
     */
    virtual ~TileRenderer() = default;
    
    /**
     * @brief Initialize the tile renderer
     * 
     * Sets up OpenGL state and prepares tile rendering pipeline
     * 
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize() = 0;
    
    /**
     * @brief Begin rendering a new frame
     * 
     * Sets up rendering state for tile rendering
     */
    virtual void BeginFrame() = 0;
    
    /**
     * @brief End rendering current frame
     * 
     * Finalizes tile rendering
     */
    virtual void EndFrame() = 0;
    
    /**
     * @brief Set tile manager for texture access
     *
     * @param tile_manager Pointer to tile manager (non-owning)
     */
    virtual void SetTileManager(TileManager* tile_manager) = 0;

    /**
     * @brief Set texture coordinator for tile texture management
     *
     * @param coordinator Pointer to texture coordinator (non-owning)
     */
    virtual void SetTextureCoordinator(TileTextureCoordinator* coordinator) = 0;

    /**
     * @brief Set globe mesh to render tiles on
     *
     * CRITICAL: Tile renderer MUST use the provided mesh geometry, not generate its own.
     * This ensures tiles are rendered on the actual displaced geometry with elevation data.
     *
     * @param globe_mesh Pointer to globe mesh (non-owning)
     */
    virtual void SetGlobeMesh(GlobeMesh* globe_mesh) = 0;

    /**
     * @brief Update visible tiles based on camera position
     * 
     * @param view_matrix Current camera view matrix
     * @param projection_matrix Current camera projection matrix  
     * @param camera_position Current camera position in world space
     * @param frustum Current camera frustum for culling
     */
    virtual void UpdateVisibleTiles(const glm::mat4& view_matrix,
                                const glm::mat4& projection_matrix,
                                const glm::vec3& camera_position,
                                const Frustum& frustum) = 0;
    
    /**
     * @brief Render all visible tiles
     * 
     * @param view_matrix Current camera view matrix
     * @param projection_matrix Current camera projection matrix
     */
    virtual void RenderTiles(const glm::mat4& view_matrix,
                         const glm::mat4& projection_matrix) = 0;
    
    /**
     * @brief Get rendering statistics
     * 
     * @return TileRenderStats Current tile rendering statistics
     */
    virtual TileRenderStats GetStats() const = 0;
    
    /**
     * @brief Get current rendering configuration
     * 
     * @return TileRenderConfig Current configuration
     */
    virtual TileRenderConfig GetConfig() const = 0;
    
    /**
     * @brief Update rendering configuration
     * 
     * @param config New configuration to apply
     */
    virtual void SetConfig(const TileRenderConfig& config) = 0;
    
    /**
     * @brief Clear all cached tile data
     */
    virtual void ClearCache() = 0;
    
    /**
     * @brief Get tile at screen coordinates
     * 
     * @param screen_x Screen X coordinate
     * @param screen_y Screen Y coordinate
     * @param screen_width Screen width in pixels
     * @param screen_height Screen height in pixels
     * @param view_matrix Current camera view matrix
     * @param projection_matrix Current camera projection matrix
     * @return TileCoordinates Tile coordinates at screen position (invalid if none)
     */
    virtual TileCoordinates GetTileAtScreenCoords(float screen_x,
                                                  float screen_y,
                                                  std::uint32_t screen_width,
                                                  std::uint32_t screen_height,
                                                  const glm::mat4& view_matrix,
                                                  const glm::mat4& projection_matrix) = 0;
    
    /**
     * @brief Get globe texture for rendering
     * 
     * @return std::uint32_t OpenGL texture ID for globe (0 if none)
     */
    virtual std::uint32_t GetGlobeTexture() const = 0;

protected:
    /**
     * @brief Protected constructor to enforce factory pattern
     */
    TileRenderer() = default;
};

} // namespace earth_map
