#pragma once

/**
 * @file renderer.h
 * @brief Rendering engine interface and core rendering components
 * 
 * Defines the main renderer interface and related classes for handling
 * OpenGL rendering operations, shader management, and GPU resource handling.
 */

#include <earth_map/math/bounding_box.h>
#include <earth_map/math/frustum.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace earth_map {

// Forward declarations
struct Configuration;
class ShaderManager;
class TileRenderer;
class PlacemarkRenderer;
class LODManager;
class GPUResourceManager;

/**
 * @brief Rendering statistics for performance monitoring
 */
struct RenderStats {
    /** Number of frames rendered in the last second */
    std::uint32_t frames_per_second = 0;
    
    /** Time taken to render the last frame in milliseconds */
    double frame_time_ms = 0.0;
    
    /** Number of draw calls in the last frame */
    std::uint32_t draw_calls = 0;
    
    /** Number of triangles rendered in the last frame */
    std::uint32_t triangles_rendered = 0;
    
    /** Number of vertices processed in the last frame */
    std::uint32_t vertices_processed = 0;
    
    /** GPU memory usage in MB */
    std::size_t gpu_memory_mb = 0;
    
    /** Number of tiles currently in memory */
    std::size_t tiles_loaded = 0;
    
    /** Number of placemarks currently rendered */
    std::size_t placemarks_rendered = 0;
};

/**
 * @brief Main renderer interface
 * 
 * Coordinates all rendering operations including tiles, placemarks,
 * and UI elements. Manages GPU resources and rendering state.
 */
class Renderer {
public:
    /**
     * @brief Create a new renderer instance
     * 
     * @param config Configuration parameters for renderer initialization
     * @return std::unique_ptr<Renderer> New renderer instance
     */
    static std::unique_ptr<Renderer> Create(const Configuration& config);
    
    /**
     * @brief Virtual destructor
     */
    virtual ~Renderer() = default;
    
    /**
     * @brief Initialize the renderer
     * 
     * Sets up OpenGL state, loads shaders, and creates resources
     * 
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize() = 0;
    
    /**
     * @brief Begin rendering a new frame
     * 
     * Sets up rendering state for a new frame
     */
    virtual void BeginFrame() = 0;
    
    /**
     * @brief End rendering the current frame
     * 
     * Finalizes rendering and presents the frame
     */
    virtual void EndFrame() = 0;
    
    /**
     * @brief Render frame with current camera state
     * 
     * Convenience method that performs frame rendering with current state
     */
    virtual void Render() = 0;
    
    /**
     * @brief Render the scene
     * 
     * Renders all visible tiles and placemarks
     * 
     * @param view_matrix Camera view matrix
     * @param projection_matrix Camera projection matrix
     * @param frustum Camera frustum for culling
     */
    virtual void RenderScene(const glm::mat4& view_matrix,
                           const glm::mat4& projection_matrix,
                           const Frustum& frustum) = 0;
    
    /**
     * @brief Resize the rendering viewport
     * 
     * @param width New viewport width in pixels
     * @param height New viewport height in pixels
     */
    virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
    
    /**
     * @brief Get rendering statistics
     * 
     * @return RenderStats Current performance statistics
     */
    virtual RenderStats GetStats() const = 0;
    
    /**
     * @brief Get the shader manager
     * 
     * @return ShaderManager* Pointer to shader manager (non-owning)
     */
    virtual ShaderManager* GetShaderManager() = 0;
    
    /**
     * @brief Get the tile renderer
     * 
     * @return TileRenderer* Pointer to tile renderer (non-owning)
     */
    virtual TileRenderer* GetTileRenderer() = 0;
    
    /**
     * @brief Get the placemark renderer
     * 
     * @return PlacemarkRenderer* Pointer to placemark renderer (non-owning)
     */
    virtual PlacemarkRenderer* GetPlacemarkRenderer() = 0;
    
    /**
     * @brief Get the LOD manager
     * 
     * @return LODManager* Pointer to LOD manager (non-owning)
     */
    virtual LODManager* GetLODManager() = 0;
    
    /**
     * @brief Get the GPU resource manager
     * 
     * @return GPUResourceManager* Pointer to GPU resource manager (non-owning)
     */
    virtual GPUResourceManager* GetGPUResourceManager() = 0;

protected:
    /**
     * @brief Protected constructor to enforce factory pattern
     */
    Renderer() = default;
};

/**
 * @brief Shader program types
 */
enum class ShaderType {
    BASIC,           ///< Basic unlit shader
    TERRAIN,         ///< Terrain with lighting
    PLACEMARK_POINT, ///< Point placemarks
    PLACEMARK_LINE,  ///< Linestring placemarks
    PLACEMARK_POLYGON, ///< Polygon placemarks
    UI,              ///< UI elements
    COUNT            ///< Number of shader types (for array sizing)
};

/**
 * @brief Shader manager interface
 * 
 * Handles loading, compiling, and managing GLSL shader programs
 */
class ShaderManager {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~ShaderManager() = default;
    
    /**
     * @brief Load all shaders
     * 
     * @return true if all shaders loaded successfully, false otherwise
     */
    virtual bool LoadShaders() = 0;
    
    /**
     * @brief Get a shader program by type
     * 
     * @param type Shader type to retrieve
     * @return std::uint32_t OpenGL shader program ID
     */
    virtual std::uint32_t GetShader(ShaderType type) = 0;
    
    /**
     * @brief Use a shader program for rendering
     * 
     * @param type Shader type to use
     * @return true if shader was activated successfully, false otherwise
     */
    virtual bool UseShader(ShaderType type) = 0;
    
    /**
     * @brief Reload all shaders (for development)
     * 
     * @return true if reload succeeded, false otherwise
     */
    virtual bool ReloadShaders() = 0;

protected:
    /**
     * @brief Protected constructor
     */
    ShaderManager() = default;
};

/**
 * @brief Quality settings for rendering
 */
enum class RenderQuality {
    LOW,      ///< Low quality, maximum performance
    MEDIUM,   ///< Balanced quality and performance
    HIGH,     ///< High quality
    ULTRA     ///< Maximum quality
};

/**
 * @brief Rendering performance settings
 */
struct RenderSettings {
    /** Rendering quality level */
    RenderQuality quality = RenderQuality::MEDIUM;
    
    /** Enable vertical sync */
    bool enable_vsync = true;
    
    /** Maximum frames per second (0 = unlimited) */
    std::uint32_t max_fps = 0;
    
    /** Enable anti-aliasing */
    bool enable_antialiasing = true;
    
    /** Number of samples for MSAA (0, 2, 4, 8, 16) */
    std::uint32_t msaa_samples = 4;
    
    /** Enable anisotropic filtering */
    bool enable_anisotropic_filtering = true;
    
    /** Maximum anisotropy level */
    std::uint32_t max_anisotropy = 16;
    
    /** Enable wireframe rendering */
    bool wireframe_mode = false;
    
    /** Enable debug rendering (bounding boxes, etc.) */
    bool enable_debug_rendering = false;
};

} // namespace earth_map