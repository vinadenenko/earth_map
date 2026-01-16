#pragma once

/**
 * @file earth_map.h
 * @brief Main public interface for the Earth Map library
 * 
 * This header provides the primary interface for the Earth Map 3D tile renderer.
 * It includes factory methods for creating map instances and core configuration.
 * 
 * @author Earth Map Team
 * @version 0.1.0
 * @date 2024
 */

#include <memory>
#include <string>
#include <cstdint>

namespace earth_map {

// Forward declarations
class Renderer;
class SceneManager;
class CameraController;
class DataParser;

/**
 * @brief Configuration structure for Earth Map initialization
 */
struct Configuration {
    /** Screen/window width in pixels */
    std::uint32_t screen_width = 1920;
    
    /** Screen/window height in pixels */
    std::uint32_t screen_height = 1080;
    
    /** Whether to enable OpenGL debug output */
    bool enable_opengl_debug = false;
    
    /** Maximum memory usage for cache in MB */
    std::size_t max_cache_memory_mb = 512;
    
    /** Maximum number of tiles to keep in memory */
    std::size_t max_tile_count = 1000;
    
    /** Path to cache directory for tiles */
    std::string cache_directory = "./cache";
    
    /** User agent string for tile requests */
    std::string user_agent = "EarthMap/0.1.0";
    
    /** Enable performance monitoring */
    bool enable_performance_monitoring = true;
};

/**
 * @brief Main Earth Map interface class
 * 
 * This is the primary interface for the Earth Map library. It provides
 * access to all major subsystems including rendering, scene management,
 * and data processing.
 * 
 * Thread Safety: This class is not thread-safe. All calls must be made
 * from a single thread, typically the main rendering thread.
 */
class EarthMap {
public:
    /**
     * @brief Factory method to create a new Earth Map instance
     * 
     * @param config Configuration parameters
     * @return std::unique_ptr<EarthMap> New Earth Map instance
     * @throws std::runtime_error if initialization fails
     */
    static std::unique_ptr<EarthMap> Create(const Configuration& config = Configuration{});
    
    /**
     * @brief Virtual destructor
     */
    virtual ~EarthMap() = default;
    
    /**
     * @brief Initialize the rendering system
     * 
     * Must be called before any rendering operations. Sets up OpenGL
     * context, creates renderer, and initializes subsystems.
     * 
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize() = 0;
    
    /**
     * @brief Render one frame
     * 
     * Renders the current scene. This should be called once per frame
     * in the main render loop.
     */
    virtual void Render() = 0;
    
    /**
     * @brief Resize the viewport
     * 
     * Called when the window/viewport size changes
     * 
     * @param width New viewport width in pixels
     * @param height New viewport height in pixels
     */
    virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
    
    /**
     * @brief Get the renderer instance
     * 
     * @return Renderer* Pointer to the renderer (non-owning)
     */
    virtual Renderer* GetRenderer() = 0;
    
    /**
     * @brief Get the scene manager instance
     * 
     * @return SceneManager* Pointer to the scene manager (non-owning)
     */
    virtual SceneManager* GetSceneManager() = 0;
    
    /**
     * @brief Get the camera controller instance
     * 
     * @return CameraController* Pointer to the camera controller (non-owning)
     */
    virtual CameraController* GetCameraController() = 0;
    
    /**
     * @brief Load data from a file
     * 
     * Supports KML, KMZ, and GeoJSON formats
     * 
     * @param file_path Path to the data file
     * @return true if loading succeeded, false otherwise
     */
    virtual bool LoadData(const std::string& file_path) = 0;
    
    /**
     * @brief Get current performance statistics
     * 
     * @return std::string JSON-formatted performance data
     */
    virtual std::string GetPerformanceStats() const = 0;

protected:
    /**
     * @brief Protected constructor to enforce factory pattern
     */
    EarthMap() = default;
};

} // namespace earth_map
