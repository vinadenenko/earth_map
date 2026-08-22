#pragma once

/**
 * @file renderer.h
 * @brief Rendering engine interface and core rendering components
 * 
 * Defines the main renderer interface and related classes for handling
 * OpenGL rendering operations, shader management, and GPU resource handling.
 */

#include "earth_map/math/bounding_box.h"
#include "earth_map/core/camera_controller.h"
#include <earth_map/renderer/performance_stats.h>
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
class ElevationManager;

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
     * Precondition: a valid OpenGL context must already be current on the
     * calling thread, with its function pointers already resolved (e.g.
     * via glewInit() on desktop; GLES entry points are directly linked on
     * Android, nothing to resolve). This library does not load GL function
     * pointers itself -- that is the host application's responsibility,
     * since it's the host that created the context, and only the host
     * knows how: this library cannot know, and must not guess, whether
     * that context is core or compatibility profile, desktop GL or GLES,
     * or which windowing system (GLX/EGL/WGL) underlies it.
     *
     * Concretely, on desktop with GLEW: if the host's context is a core
     * profile (as both basic_example.cpp's GLFW window and qt-test-app's
     * Qt Quick RHI window are), the host must set glewExperimental = GL_TRUE
     * before calling glewInit() -- GLEW's classic extension-string query
     * (glGetString(GL_EXTENSIONS)) is invalid on core-profile contexts and
     * silently produces wrong/incomplete results without it. This has
     * already been missed once by one of this repository's own two example
     * hosts; if you are adding a third host, do not skip it.
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
                             const glm::mat4& projection_matrix) = 0;
    
    /**
     * @brief Resize the rendering viewport
     * 
     * @param width New viewport width in pixels
     * @param height New viewport height in pixels
     */
    virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
    
    /**
     * @brief Get per-frame performance statistics: fps, CPU/GPU frame
     * time, and a per-subsystem timing breakdown (see
     * earth_map/renderer/performance_stats.h). Populated only when the
     * library was built with EARTH_MAP_ENABLE_PERFORMANCE_MONITORING;
     * otherwise always default-constructed (zeroed, no zones), at no
     * runtime cost.
     *
     * Not synchronized: this reflects the most recent Render()/EndFrame()
     * call with no locking, so call it from the same thread that calls
     * Render() (e.g. immediately after it, as basic_example and the
     * qt-test-app example both do). Reading it from a different thread
     * while Render() may be running concurrently is a data race.
     *
     * @return PerformanceStats Current performance statistics
     */
    virtual PerformanceStats GetStats() const = 0;
    
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
     * @brief Get GPU resource manager
     * 
     * @return GPUResourceManager* Pointer to GPU resource manager (non-owning)
     */
    virtual GPUResourceManager* GetGPUResourceManager() = 0;
    
    /**
     * @brief Get camera controller
     * 
     * @return CameraController* Pointer to camera controller (non-owning)
     */
    virtual CameraController* GetCameraController() const = 0;
    
    /**
     * @brief Set camera controller for integration
     * 
     * @param camera_controller Pointer to camera controller
     */
    virtual void SetCameraController(CameraController* camera_controller) = 0;

    /**
      * @brief Enable or disable mini-map rendering
      *
      * @param enabled true to enable mini-map, false to disable
      */
    virtual void SetMiniMapEnabled(bool enabled) = 0;

    /**
     * @brief Get the elevation manager
     *
     * @return ElevationManager* Pointer to elevation manager (non-owning), nullptr if not initialized
     */
    virtual ElevationManager* GetElevationManager() = 0;

    /**
     * @brief Enable or disable elevation rendering
     *
     * @param enabled true to enable elevation rendering, false to disable
     */
    virtual void SetElevationEnabled(bool enabled) = 0;

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
