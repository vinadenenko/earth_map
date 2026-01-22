#pragma once

/**
 * @file mini_map_renderer.h
 * @brief Mini-map renderer for global context overlay
 *
 * Renders a small 2D overview showing camera position and view frustum
 * on a textured Earth globe, similar to NASA World Wind mini-map.
 */

#include <earth_map/renderer/renderer.h>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>

namespace earth_map {

class ShaderManager;
class CameraController;

/**
 * @brief Mini-map renderer for global context
 *
 * Provides a 2D overview mini-map showing:
 * - Textured Earth globe
 * - Current camera position (red dot)
 * - Camera view frustum outline
 * - Optional lat/lon grid
 */
class MiniMapRenderer {
public:
    /**
     * @brief Configuration for mini-map rendering
     */
    struct Config {
        uint32_t width = 256;           ///< Mini-map width in pixels
        uint32_t height = 256;          ///< Mini-map height in pixels
        uint32_t offset_x = 10;         ///< X offset from top-right corner
        uint32_t offset_y = 10;         ///< Y offset from top-right corner
        bool show_grid = true;          ///< Show lat/lon grid overlay
        float grid_opacity = 0.3f;      ///< Grid line opacity
    };

    /**
     * @brief Constructor
     *
     * @param config Mini-map configuration
     */
    explicit MiniMapRenderer(const Config& config);

    /**
     * @brief Destructor
     */
    ~MiniMapRenderer();

    /**
     * @brief Initialize the mini-map renderer
     *
     * @param shader_program OpenGL shader program for 2D rendering
     * @return true if initialization successful
     */
    bool Initialize(uint32_t shader_program);

    /**
     * @brief Update mini-map with current camera state
     *
     * @param camera_controller Current camera controller
     * @param screen_width Main viewport width
     * @param screen_height Main viewport height
     */
    void Update(CameraController* camera_controller,
                uint32_t screen_width, uint32_t screen_height);

    /**
     * @brief Render mini-map to screen
     *
     * @param aspect_ratio Viewport aspect ratio for frustum calculation
     */
    void Render(float aspect_ratio);

    /**
     * @brief Render camera frustum on mini-map
     *
     * @param aspect_ratio Viewport aspect ratio for frustum calculation
     */
    void RenderFrustum(float aspect_ratio);

    /**
     * @brief Set mini-map size
     *
     * @param width New width in pixels
     * @param height New height in pixels
     */
    void SetSize(uint32_t width, uint32_t height);

    /**
     * @brief Set mini-map offset from top-right corner
     *
     * @param offset_x X offset in pixels
     * @param offset_y Y offset in pixels
     */
    void SetOffset(uint32_t offset_x, uint32_t offset_y);

    /**
     * @brief Get mini-map texture for rendering
     *
     * @return OpenGL texture ID
     */
    uint32_t GetTexture() const { return texture_; }

    /**
     * @brief Get current configuration
     *
     * @return Current config (const reference)
     */
    const Config& GetConfig() const { return config_; }

private:
    /**
     * @brief Load Earth texture
     *
     * @return true if texture loaded successfully
     */
    bool LoadEarthTexture();

    /**
     * @brief Generate fallback wireframe globe
     */
    void GenerateFallbackGlobe();

    /**
     * @brief Create mini-map geometry (quad)
     */
    void CreateGeometry();

    /**
     * @brief Update camera position marker
     *
     * @param camera_controller Camera controller for position
     */
    void UpdateCameraPosition(CameraController* camera_controller);

    /**
     * @brief Update frustum outline
     *
     * @param camera_controller Camera controller for frustum
     */
    void UpdateFrustumOutline(CameraController* camera_controller);

    /**
     * @brief Render globe texture/geometry
     */
    void RenderGlobe();

    /**
     * @brief Render grid overlay
     */
    void RenderGrid();

    /**
     * @brief Render camera position marker
     */
    void RenderCameraPosition();

    /**
     * @brief Render frustum outline
     */
    void RenderFrustumOutline();

    /**
     * @brief Convert lat/lon to mini-map pixel coordinates
     *
     * @param latitude Latitude in degrees (-90 to 90)
     * @param longitude Longitude in degrees (-180 to 180)
     * @return glm::vec2 Pixel coordinates in mini-map space
     */
    glm::vec2 LatLonToPixel(float latitude, float longitude) const;

    Config config_;                    ///< Current configuration
    uint32_t shader_program_;           ///< OpenGL shader program
    CameraController* camera_controller_ = nullptr; ///< Current camera controller

    // OpenGL objects
    uint32_t framebuffer_ = 0;         ///< Mini-map framebuffer
    uint32_t texture_ = 0;             ///< Mini-map texture
    uint32_t earth_texture_ = 0;       ///< Earth texture
    uint32_t vao_ = 0;                 ///< Vertex array object
    uint32_t vbo_ = 0;                 ///< Vertex buffer object
    uint32_t grid_vao_ = 0;            ///< Grid vertex array
    uint32_t grid_vbo_ = 0;            ///< Grid vertex buffer

    // Rendering data
    glm::vec2 camera_position_pixels_; ///< Camera position in pixels

    bool initialized_ = false;         ///< Initialization status
    bool texture_loaded_ = false;      ///< Earth texture loaded successfully
};

} // namespace earth_map