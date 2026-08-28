#pragma once

/**
 * @file tile_renderer.h
 * @brief Tile rendering system for globe
 * 
 * Defines the tile renderer interface and structures for rendering
 * map tiles onto a 3D globe mesh with proper LOD management.
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/geodesy/wgs84_ellipsoid.h>
#include <earth_map/renderer/performance_stats.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace earth_map {

// Forward declarations
class TileManager;
class TileTextureCoordinator;
struct Frustum;

/**
 * @brief Tile rendering statistics
 */
struct TileRenderStats {
    /** Number of tiles currently visible */
    std::size_t visible_tiles = 0;
    
    /** Number of visible tiles whose texture is ready (loaded, not a fallback) */
    std::size_t rendered_tiles = 0;

    /** Average LOD level of visible tiles */
    float average_lod = 0.0f;

    /** Number of tile pool layers currently occupied by resident tiles */
    std::uint32_t occupied_pool_layers = 0;

    /** Fixed tile pool layer capacity */
    std::uint32_t max_pool_layers = 0;

    /** Tile pool GPU memory currently used, in bytes */
    std::uint64_t tile_pool_bytes_used = 0;

    /** Tile pool GPU memory budget if fully occupied, in bytes */
    std::uint64_t tile_pool_bytes_max = 0;

    /** Decoded pages waiting before and after this frame's upload pass */
    std::size_t upload_queue_depth_before = 0;
    std::size_t upload_queue_depth_after = 0;

    /** Requests still loading or decoded-but-not-installed */
    std::size_t pending_tile_loads = 0;

    /** Per-frame bounded upload pass activity */
    std::size_t upload_commands_processed = 0;
    std::size_t upload_commands_installed = 0;
    std::size_t tile_pool_upload_attempts = 0;
    std::uint64_t tile_pool_upload_attempt_bytes = 0;

    /** Longest decoded-page wait among commands processed this frame */
    double upload_max_queue_wait_ms = 0.0;

    /** Whole-command render-thread CPU time for this frame's upload pass */
    double upload_total_command_cpu_ms = 0.0;
    double upload_max_command_cpu_ms = 0.0;

    /** CPU time inside TileTexturePool::UploadTile this frame */
    double tile_pool_upload_total_cpu_ms = 0.0;
    double tile_pool_upload_max_cpu_ms = 0.0;

    /** Remaining upload-command stages, kept separate for diagnosis */
    double upload_eviction_total_cpu_ms = 0.0;
    double upload_eviction_max_cpu_ms = 0.0;
    double upload_residency_state_total_cpu_ms = 0.0;
    double upload_residency_state_max_cpu_ms = 0.0;
};

/**
 * @brief Fragment paths used to attribute direct-imagery GPU cost.
 *
 * FullImagery is the normal renderer.  The other values are development
 * probes: each is compiled as a separate shader program, so selecting one
 * does not put a runtime branch into the normal fragment path.  They are
 * useful only with performance monitoring enabled and must never be used to
 * judge imagery correctness.
 */
enum class TileFragmentShadingProbe : std::uint8_t {
    FullImagery,
    FlatFill,
    PatchLocalCoordinates,
    UnlitImagery,
};

/**
 * @brief Tile rendering configuration
 */
struct TileRenderConfig {
    std::uint32_t max_visible_tiles = 1000;     ///< Maximum tiles to render simultaneously
    /// Maximum logical imagery uploads processed in one render frame. Hosts
    /// should choose this for their GPU class: 8 is the desktop baseline;
    /// constrained mobile hosts commonly use 1.
    std::uint32_t max_tile_uploads_per_frame = 8;
    float tile_fade_distance = 2.0f;           ///< Distance for tile fade in/out
    bool enable_lod_transitions = true;           ///< Enable smooth LOD transitions
    float min_lod_distance = 100.0f;          ///< Minimum distance for LOD switching
    float max_lod_distance = 10000.0f;         ///< Maximum distance for LOD switching
    TileFragmentShadingProbe fragment_shading_probe =
        TileFragmentShadingProbe::FullImagery; ///< Development-only GPU attribution probe
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
     * @brief Set texture coordinator for tile texture management
     *
     * @param coordinator Pointer to texture coordinator (non-owning)
     */
    virtual void SetTextureCoordinator(TileTextureCoordinator* coordinator) = 0;

    /**
     * @brief Update visible tiles based on camera position
     * 
     * @param view_matrix Current camera view matrix
     * @param projection_matrix Current camera projection matrix  
     * @param camera_position Current WGS84 ECEF camera position in metres
     * @param frustum Current camera frustum for culling
     */
    virtual void UpdateVisibleTiles(const glm::mat4& view_matrix,
                                    const glm::mat4& projection_matrix,
                                    const geodesy::EcefPosition& camera_position) = 0;
    
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
     * @brief Get this tile renderer's per-zone CPU/GPU timing breakdown
     * for the last frame (e.g. "tile.cull", "tile.upload", "tile.draw").
     * Renderer::GetStats() merges this into its own PerformanceStats.zones
     * -- see earth_map/renderer/performance_stats.h. Empty when the
     * library was built without EARTH_MAP_ENABLE_PERFORMANCE_MONITORING.
     *
     * @return std::vector<FrameZoneTiming> This subsystem's zones
     */
    virtual std::vector<FrameZoneTiming> GetZoneTimings() const = 0;

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
