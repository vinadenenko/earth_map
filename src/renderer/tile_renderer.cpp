/**
 * @file tile_renderer.cpp
 * @brief Tile rendering system implementation
 */

#include <earth_map/renderer/tile_renderer.h>
#include <earth_map/renderer/globe_mesh.h>
#include <earth_map/math/coordinate_system.h>
#include <earth_map/math/projection.h>
#include <earth_map/math/tile_mathematics.h>
#include <spdlog/spdlog.h>
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace earth_map {

/**
 * @brief Tile rendering state
 */
struct TileRenderState {
    TileCoordinates coordinates;        ///< Tile coordinates (x, y, zoom)
    std::uint32_t texture_id;       ///< OpenGL texture ID
    BoundingBox2D geographic_bounds;   ///< Geographic bounds of this tile
    float lod_level;                 ///< Level of detail for this tile
    bool is_visible;                 ///< Whether tile is currently visible
    float last_used;                 ///< Last frame this tile was used
    float load_priority;              ///< Priority for loading (0.0 = highest)
};

/**
 * @brief Basic tile renderer implementation
 */
class TileRendererImpl : public TileRenderer {
public:
    explicit TileRendererImpl(const TileRenderConfig& config) 
        : config_(config), frame_counter_(0) {
        spdlog::info("Creating tile renderer with max tiles: {}", config.max_visible_tiles);
    }
    
    ~TileRendererImpl() override {
        Cleanup();
    }
    
    bool Initialize() override {
        if (initialized_) {
            return true;
        }
        
        spdlog::info("Initializing tile renderer");
        
        try {
            if (!InitializeOpenGLState()) {
                spdlog::error("Failed to initialize OpenGL state");
                return false;
            }
            
            initialized_ = true;
            spdlog::info("Tile renderer initialized successfully");
            return true;
            
        } catch (const std::exception& e) {
            spdlog::error("Exception during tile renderer initialization: {}", e.what());
            return false;
        }
    }
    
    void BeginFrame() override {
        if (!initialized_) {
            return;
        }
        
        frame_counter_++;
        stats_.render_time_ms = 0.0f;
        stats_.rendered_tiles = 0;
        stats_.texture_binds = 0;
        
        // Debug: Check if we have tiles loaded
        if (frame_counter_ % 60 == 0) {
            spdlog::info("Tile renderer debug - visible_tiles: {}, texture_cache_size: {}", 
                        visible_tiles_.size(), texture_cache_.size());
        }
    }
    
    void EndFrame() override {
        if (!initialized_) {
            return;
        }
        
        // Update statistics
        stats_.visible_tiles = visible_tiles_.size();
        
        // Calculate average LOD
        if (visible_tiles_.size() > 0) {
            float total_lod = 0.0f;
            for (const auto& tile : visible_tiles_) {
                total_lod += tile.lod_level;
            }
            stats_.average_lod = total_lod / static_cast<float>(visible_tiles_.size());
        } else {
            stats_.average_lod = 0.0f;
        }
    }
    
    void SetTileManager(TileManager* tile_manager) override {
        tile_manager_ = tile_manager;
    }
    
    void UpdateVisibleTiles(const glm::mat4& view_matrix,
                        const glm::mat4& projection_matrix,
                        const glm::vec3& camera_position,
                        const Frustum& frustum) override {
        if (!initialized_ || !tile_manager_) {
            return;
        }
        
        // Clear previous visible tiles
        visible_tiles_.clear();
        
        // Calculate camera distance from globe center
        const float camera_distance = glm::length(camera_position);
        
        // Estimate optimal zoom level based on distance
        const int zoom_level = CalculateOptimalZoom(camera_distance);
        
        // Calculate geographic bounds of visible area
        const BoundingBox2D visible_bounds = CalculateVisibleGeographicBounds(
            view_matrix, projection_matrix);
        
        // Get tiles in visible bounds at appropriate zoom
        const std::vector<TileCoordinates> candidate_tiles =
            tile_manager_->GetTilesInBounds(visible_bounds, zoom_level);
        
        // Filter tiles by frustum culling and limit
        std::size_t tiles_added = 0;
        for (const TileCoordinates& tile_coords : candidate_tiles) {
            if (tiles_added >= config_.max_visible_tiles) {
                break;
            }
            
            // Check if tile is in frustum
            if (IsTileInFrustum(tile_coords, frustum)) {
                TileRenderState tile_state;
                tile_state.coordinates = tile_coords;
                tile_state.geographic_bounds = TileMathematics::GetTileBounds(tile_coords);
                tile_state.lod_level = CalculateTileLOD(tile_coords, camera_distance);
                tile_state.last_used = static_cast<float>(frame_counter_);
                tile_state.load_priority = CalculateLoadPriority(tile_coords, camera_position);
                tile_state.is_visible = true;
                
                // Get texture from tile manager
                tile_state.texture_id = tile_manager_->GetTileTexture(tile_coords);
                
                // DEBUG: Force texture creation for testing
                if (tile_state.texture_id == 0) {
                    tile_state.texture_id = CreateTestTexture();
                }
                
                visible_tiles_.push_back(tile_state);
                tiles_added++;
            }
        }
        
        // Sort tiles by priority (highest first)
        std::sort(visible_tiles_.begin(), visible_tiles_.end(),
                 [](const TileRenderState& a, const TileRenderState& b) {
                     return a.load_priority < b.load_priority;
                 });
        
        spdlog::debug("Tile renderer update: {} visible tiles, zoom level {}", 
                    visible_tiles_.size(), zoom_level);
    }
    
    void RenderTiles(const glm::mat4& view_matrix,
                     const glm::mat4& projection_matrix) override {
        if (!initialized_ || visible_tiles_.empty()) {
            return;
        }
        
        // Save current OpenGL state
        GLboolean depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cull_face_enabled = glIsEnabled(GL_CULL_FACE);
        
        // Enable required states
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        
        // Set up shader for textured rendering
        glUseProgram(tile_shader_program_);
        
        // Set matrices
        const GLint view_loc = glGetUniformLocation(tile_shader_program_, "uView");
        const GLint proj_loc = glGetUniformLocation(tile_shader_program_, "uProjection");
        const GLint model_loc = glGetUniformLocation(tile_shader_program_, "uModel");
        
        glUniformMatrix4fv(view_loc, 1, GL_FALSE, glm::value_ptr(view_matrix));
        glUniformMatrix4fv(proj_loc, 1, GL_FALSE, glm::value_ptr(projection_matrix));
        glUniformMatrix4fv(model_loc, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
        
        // Bind globe mesh VAO
        glBindVertexArray(globe_vao_);
        
        // Render each visible tile
        for (const auto& tile : visible_tiles_) {
            if (tile.texture_id > 0) {
                RenderSingleTile(tile, view_matrix, projection_matrix);
                stats_.rendered_tiles++;
            }
        }
        
        // Unbind VAO
        glBindVertexArray(0);
        
        // Restore previous OpenGL state
        if (!depth_test_enabled) {
            glDisable(GL_DEPTH_TEST);
        }
        if (!cull_face_enabled) {
            glDisable(GL_CULL_FACE);
        }
        
        stats_.texture_binds = stats_.rendered_tiles;
    }
    
    TileRenderStats GetStats() const override {
        return stats_;
    }
    
    TileRenderConfig GetConfig() const override {
        return config_;
    }
    
    void SetConfig(const TileRenderConfig& config) override {
        config_ = config;
        spdlog::info("Tile renderer config updated: max_tiles={}", 
                    config_.max_visible_tiles);
    }
    
    void ClearCache() override {
        visible_tiles_.clear();
        texture_cache_.clear();
        spdlog::info("Tile renderer cache cleared");
    }
    
    TileCoordinates GetTileAtScreenCoords(float screen_x,
                                          float screen_y,
                                          std::uint32_t screen_width,
                                          std::uint32_t screen_height,
                                          const glm::mat4& view_matrix,
                                          const glm::mat4& projection_matrix) override {
        // Ray casting implementation would go here
        // For now, return invalid tile coordinates
        (void)screen_x;
        (void)screen_y;
        (void)screen_width;
        (void)screen_height;
        (void)view_matrix;
        (void)projection_matrix;
        return TileCoordinates();
    }

private:
    TileRenderConfig config_;
    TileManager* tile_manager_ = nullptr;
    bool initialized_ = false;
    std::uint64_t frame_counter_ = 0;
    std::vector<TileRenderState> visible_tiles_;
    TileRenderStats stats_;
    
    // OpenGL objects
    std::uint32_t tile_shader_program_ = 0;
    std::uint32_t globe_vao_ = 0;
    std::uint32_t globe_vbo_ = 0;
    std::uint32_t globe_ebo_ = 0;
    std::unordered_map<TileCoordinates, std::uint32_t, TileCoordinatesHash> texture_cache_;
    
    bool InitializeOpenGLState() {
        // Create simple textured shader
        const char* textured_vertex_shader = R"(
            #version 330 core
            layout (location = 0) in vec3 aPos;
            layout (location = 1) in vec3 aNormal;
            layout (location = 2) in vec2 aTexCoord;
            
            uniform mat4 uModel;
            uniform mat4 uView;
            uniform mat4 uProjection;
            uniform sampler2D uTileTexture;
            
            out vec3 FragPos;
            out vec3 Normal;
            out vec2 TexCoord;
            
            void main() {
                FragPos = vec3(uModel * vec4(aPos, 1.0));
                Normal = mat3(transpose(inverse(uModel))) * aNormal;
                TexCoord = aTexCoord;
                
                gl_Position = uProjection * uView * vec4(FragPos, 1.0);
            }
        )";
        
        const char* textured_fragment_shader = R"(
            #version 330 core
            in vec3 FragPos;
            in vec3 Normal;
            in vec2 TexCoord;
            
            out vec4 FragColor;
            
            uniform sampler2D uTileTexture;
            uniform vec3 uLightPos;
            uniform vec3 uLightColor;
            uniform vec3 uViewPos;
            
            void main() {
                // Basic lighting
                float ambientStrength = 0.1;
                vec3 ambient = ambientStrength * uLightColor;
                
                vec3 norm = normalize(Normal);
                vec3 lightDir = normalize(uLightPos - FragPos);
                float diff = max(dot(norm, lightDir), 0.0);
                vec3 diffuse = diff * uLightColor;
                
                // Sample texture
                vec4 texColor = texture(uTileTexture, TexCoord);
                
                vec3 result = (ambient + diffuse) * texColor.rgb;
                FragColor = vec4(result, texColor.a);
            }
        )";
        
        // Compile vertex shader
        const std::uint32_t vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex_shader, 1, &textured_vertex_shader, nullptr);
        glCompileShader(vertex_shader);
        
        std::int32_t success;
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char info_log[512];
            glGetShaderInfoLog(vertex_shader, 512, nullptr, info_log);
            spdlog::error("Vertex shader compilation failed: {}", info_log);
            return false;
        }
        
        // Compile fragment shader
        const std::uint32_t fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment_shader, 1, &textured_fragment_shader, nullptr);
        glCompileShader(fragment_shader);
        
        glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char info_log[512];
            glGetShaderInfoLog(fragment_shader, 512, nullptr, info_log);
            spdlog::error("Fragment shader compilation failed: {}", info_log);
            return false;
        }
        
        // Link shader program
        tile_shader_program_ = glCreateProgram();
        glAttachShader(tile_shader_program_, vertex_shader);
        glAttachShader(tile_shader_program_, fragment_shader);
        glLinkProgram(tile_shader_program_);
        
        glGetProgramiv(tile_shader_program_, GL_LINK_STATUS, &success);
        if (!success) {
            char info_log[512];
            glGetProgramInfoLog(tile_shader_program_, 512, nullptr, info_log);
            spdlog::error("Shader program linking failed: {}", info_log);
            return false;
        }
        
        // Clean up shaders
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        
        return CreateGlobeMesh();
    }
    
    bool CreateGlobeMesh() {
        // Generate simple sphere mesh for texturing
        const float radius = 1.0f;
        const int segments = 32;
        const int rings = 16;
        
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        
        // Generate vertices with texture coordinates
        for (int r = 0; r <= rings; ++r) {
            const float theta = static_cast<float>(r) * glm::pi<float>() / rings;
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);
            
            for (int s = 0; s <= segments; ++s) {
                const float phi = static_cast<float>(s) * 2.0f * glm::pi<float>() / segments;
                const float sin_phi = std::sin(phi);
                const float cos_phi = std::cos(phi);
                
                // Position
                const float x = radius * sin_theta * cos_phi;
                const float y = radius * cos_theta;
                const float z = radius * sin_theta * sin_phi;
                
                // Normal (same as position for sphere)
                const float nx = sin_theta * cos_phi;
                const float ny = cos_theta;
                const float nz = sin_theta * sin_phi;
                
                // Texture coordinates (for globe projection)
                const float u = static_cast<float>(s) / segments;
                const float v = static_cast<float>(r) / rings;
                
                // Add vertex (position + normal + texcoord)
                vertices.insert(vertices.end(), {x, y, z, nx, ny, nz, u, v});
            }
        }
        
        // Generate indices
        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < segments; ++s) {
                const int current = r * (segments + 1) + s;
                const int next = current + segments + 1;
                
                // First triangle
                indices.insert(indices.end(), {
                    static_cast<unsigned int>(current), 
                    static_cast<unsigned int>(next), 
                    static_cast<unsigned int>(current + 1)
                });
                // Second triangle
                indices.insert(indices.end(), {
                    static_cast<unsigned int>(next), 
                    static_cast<unsigned int>(next + 1), 
                    static_cast<unsigned int>(current + 1)
                });
            }
        }
        
        // Create OpenGL objects
        glGenVertexArrays(1, &globe_vao_);
        glGenBuffers(1, &globe_vbo_);
        glGenBuffers(1, &globe_ebo_);
        
        // Bind VAO
        glBindVertexArray(globe_vao_);
        
        // Bind and fill VBO
        glBindBuffer(GL_ARRAY_BUFFER, globe_vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), 
                    vertices.data(), GL_STATIC_DRAW);
        
        // Bind and fill EBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, globe_ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), 
                    indices.data(), GL_STATIC_DRAW);
        
        // Set vertex attributes
        // Position (location = 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        
        // Normal (location = 1)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        
        // Texture coordinates (location = 2)
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        
        // Unbind VAO
        glBindVertexArray(0);
        
        return true;
    }
    
    void RenderSingleTile(const TileRenderState& tile, 
                          const glm::mat4& view_matrix,
                          const glm::mat4& projection_matrix) {
        // For now, skip tiles with no texture
        if (tile.texture_id == 0) {
            return;
        }
        
        // Calculate tile's geographic bounds
        auto bounds = tile.geographic_bounds;
        auto center = bounds.GetCenter();
        
        // Convert geographic coordinates to world position
        // This is a simplified approach - in full implementation would use proper projection
        double lon = center.x;
        double lat = center.y;
        
        // Simple conversion to 3D position on sphere
        float phi = static_cast<float>((90.0 - lat) * M_PI / 180.0);
        float theta = static_cast<float>(lon * M_PI / 180.0);
        
        glm::vec3 tile_pos = glm::vec3(
            std::cos(phi) * std::sin(theta),
            std::sin(phi),
            std::cos(phi) * std::cos(theta)
        );
        
        // Scale by tile size (simplified - should use proper tile size calculation)
        float tile_scale = 0.1f; // Small scale for testing
        
        // Create model matrix for this tile
        glm::mat4 model = glm::translate(glm::mat4(1.0f), tile_pos);
        model = glm::scale(model, glm::vec3(tile_scale));
        
        // Bind tile texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tile.texture_id);
        stats_.texture_binds++;
        
        // Set shader uniforms
        glUseProgram(tile_shader_program_);
        
        const GLint view_loc = glGetUniformLocation(tile_shader_program_, "uView");
        const GLint proj_loc = glGetUniformLocation(tile_shader_program_, "uProjection");
        const GLint model_loc = glGetUniformLocation(tile_shader_program_, "uModel");
        const GLint light_loc = glGetUniformLocation(tile_shader_program_, "uLightPos");
        const GLint color_loc = glGetUniformLocation(tile_shader_program_, "uObjectColor");
        
        glUniformMatrix4fv(view_loc, 1, GL_FALSE, glm::value_ptr(view_matrix));
        glUniformMatrix4fv(proj_loc, 1, GL_FALSE, glm::value_ptr(projection_matrix));
        glUniformMatrix4fv(model_loc, 1, GL_FALSE, glm::value_ptr(model));
        glUniform3f(light_loc, 2.0f, 2.0f, 2.0f);
        glUniform3f(color_loc, 1.0f, 1.0f, 1.0f);
        
        // Draw a simple quad for this tile
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(1.0f, -1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(1.0f, 1.0f, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, 1.0f, 0.0f);
        glEnd();
    }
    
    void Cleanup() {
        if (globe_vao_) {
            glDeleteVertexArrays(1, &globe_vao_);
            globe_vao_ = 0;
        }
        if (globe_vbo_) {
            glDeleteBuffers(1, &globe_vbo_);
            globe_vbo_ = 0;
        }
        if (globe_ebo_) {
            glDeleteBuffers(1, &globe_ebo_);
            globe_ebo_ = 0;
        }
        if (tile_shader_program_) {
            glDeleteProgram(tile_shader_program_);
            tile_shader_program_ = 0;
        }
    }
    
    int CalculateOptimalZoom(float camera_distance) const {
        // Simple distance-based zoom selection
        if (camera_distance < 1000.0f) return 15;
        if (camera_distance < 5000.0f) return 12;
        if (camera_distance < 10000.0f) return 10;
        if (camera_distance < 50000.0f) return 8;
        return 6;
    }
    
    BoundingBox2D CalculateVisibleGeographicBounds(const glm::mat4& view_matrix,
                                               const glm::mat4& projection_matrix) const {
        // For now, return full world bounds
        // In a full implementation, this would calculate actual frustum bounds
        (void)view_matrix;
        (void)projection_matrix;

        return BoundingBox2D(
            glm::dvec2(-180.0, -85.0511),
            glm::dvec2(180.0, 85.0511)
        );
    }
    
    bool IsTileInFrustum(const TileCoordinates& tile, const Frustum& frustum) const {
        // For now, return true (all tiles visible)
        // In a full implementation, this would check tile bounds against frustum
        (void)tile;
        (void)frustum;

        return true;
    }
    
    float CalculateTileLOD(const TileCoordinates& tile, float camera_distance) const {
        (void)camera_distance;
        // Simple LOD calculation based on zoom level
        return static_cast<float>(tile.zoom);
    }
    
    float CalculateLoadPriority(const TileCoordinates& tile, const glm::vec3& camera_position) const {
        (void)camera_position;
        // For now, use zoom as priority (higher zoom = higher priority)
        return static_cast<float>(30 - tile.zoom); // Invert so higher zoom = lower number = higher priority
    }
    
    std::uint32_t CreateTestTexture() {
        // Create a simple test texture (checkerboard pattern)
        const int texture_size = 256;
        std::vector<std::uint8_t> texture_data;
        texture_data.resize(texture_size * texture_size * 3); // RGB
        
        for (int y = 0; y < texture_size; ++y) {
            for (int x = 0; x < texture_size; ++x) {
                int idx = (y * texture_size + x) * 3;
                
                // Create checkerboard pattern
                bool is_white = ((x / 32) + (y / 32)) % 2 == 0;
                std::uint8_t color = is_white ? 255 : 0;
                
                texture_data[idx] = color;     // R
                texture_data[idx + 1] = color; // G  
                texture_data[idx + 2] = color; // B
            }
        }
        
        // Create OpenGL texture
        std::uint32_t texture_id;
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_size, texture_size, 0, 
                     GL_RGB, GL_UNSIGNED_BYTE, texture_data.data());
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        spdlog::info("Created test texture with ID: {}", texture_id);
        return texture_id;
    }
};

// Factory function
std::unique_ptr<TileRenderer> TileRenderer::Create(const TileRenderConfig& config) {
    return std::make_unique<TileRendererImpl>(config);
}

} // namespace earth_map
