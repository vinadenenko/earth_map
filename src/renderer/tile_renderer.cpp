/**
 * @file tile_renderer.cpp
 * @brief Tile rendering system implementation
 */

#include <earth_map/renderer/tile_renderer.h>
#include <earth_map/renderer/globe_mesh.h>
#include <earth_map/math/coordinate_system.h>
#include <earth_map/math/projection.h>
#include <earth_map/math/tile_mathematics.h>
#include <earth_map/renderer/tile_texture_manager.h>
#include <spdlog/spdlog.h>
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <atomic>
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
 * @brief Texture atlas tile information
 */
struct AtlasTileInfo {
    int x, y;                    ///< Tile coordinates
    int zoom;                     ///< Zoom level
    float u, v;                   ///< UV coordinates in atlas
    float u_size, v_size;         ///< UV size in atlas
    std::uint32_t texture_id;     ///< Original tile texture ID
};

/**
 * @brief Basic tile renderer implementation
 */
class TileRendererImpl : public TileRenderer {
public:
    explicit TileRendererImpl(const TileRenderConfig& config) 
        : config_(config), frame_counter_(0), atlas_size_(2048), tile_size_(256) {
        spdlog::info("Creating tile renderer with max tiles: {}", config.max_visible_tiles);
        CalculateAtlasLayout();
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
            // spdlog::info("Tile renderer debug - visible_tiles: {}, texture_cache_size: {}", visible_tiles_.size(), texture_cache_.size());
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
        
        // Log tile count for debugging
        spdlog::info("Candidate tiles for zoom {}: {}", zoom_level, candidate_tiles.size());
        
        // Filter tiles by frustum culling and limit with performance considerations
        std::size_t tiles_added = 0;
        std::size_t max_tiles_for_frame = std::min(static_cast<std::size_t>(config_.max_visible_tiles), 
                                                 static_cast<std::size_t>(tiles_per_row_ * tiles_per_row_));
        
        // Resize atlas tiles array
        atlas_tiles_.clear();
        atlas_tiles_.reserve(max_tiles_for_frame);
        
        for (const TileCoordinates& tile_coords : candidate_tiles) {
            if (tiles_added >= max_tiles_for_frame) {
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
                
                // Get texture from tile manager (this will trigger async loading)
                tile_state.texture_id = tile_manager_->GetTileTexture(tile_coords);

                // Create test texture if no texture available yet (async loading in progress)
                if (tile_state.texture_id == 0) {
                    tile_state.texture_id = CreateTestTexture();
                    spdlog::info("Created test texture {} for tile ({}, {}, {})",
                                 tile_state.texture_id, tile_coords.x, tile_coords.y, tile_coords.zoom);
                    
                    // Trigger async tile loading from texture manager
                    TriggerTileLoading(tile_coords);
                }
                
                visible_tiles_.push_back(tile_state);
                
                // Add to atlas tiles
                AtlasTileInfo atlas_tile;
                atlas_tile.x = tile_coords.x;
                atlas_tile.y = tile_coords.y;
                atlas_tile.zoom = tile_coords.zoom;
                atlas_tile.texture_id = tile_state.texture_id;
                atlas_tiles_.push_back(atlas_tile);
                
                tiles_added++;
            }
        }
        
        // spdlog::info("Selected {} tiles for rendering (zoom {})", tiles_added, zoom_level);
        
        // Sort tiles by priority (highest first)
        std::sort(visible_tiles_.begin(), visible_tiles_.end(),
                 [](const TileRenderState& a, const TileRenderState& b) {
                     return a.load_priority < b.load_priority;
                 });
        
        // Mark atlas as dirty only if visible tiles changed
        bool tiles_changed = false;
        
        if (visible_tiles_.size() != last_visible_tiles_.size()) {
            tiles_changed = true;
        } else {
            for (size_t i = 0; i < visible_tiles_.size(); ++i) {
                if (visible_tiles_[i].coordinates != last_visible_tiles_[i]) {
                    tiles_changed = true;
                    break;
                }
            }
        }
        
        if (tiles_changed) {
            atlas_dirty_.store(true);
            last_visible_tiles_.clear();
            for (const auto& tile : visible_tiles_) {
                last_visible_tiles_.push_back(tile.coordinates);
            }
        }
        
        spdlog::debug("Tile renderer update: {} visible tiles, zoom level {}", 
                    visible_tiles_.size(), zoom_level);
    }
    
    void RenderTiles(const glm::mat4& view_matrix,
                     const glm::mat4& projection_matrix) override {
        if (!initialized_ || visible_tiles_.empty()) {
            return;
        }
        
        // Update texture atlas if needed
        CreateTextureAtlas();
        
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
        const GLint time_loc = glGetUniformLocation(tile_shader_program_, "uTime");

        glUniformMatrix4fv(view_loc, 1, GL_FALSE, glm::value_ptr(view_matrix));
        glUniformMatrix4fv(proj_loc, 1, GL_FALSE, glm::value_ptr(projection_matrix));
        glUniformMatrix4fv(model_loc, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
        glUniform1f(time_loc, static_cast<float>(frame_counter_) * 0.016f);  // ~60fps timing

        // Set lighting uniforms
        const GLint light_loc = glGetUniformLocation(tile_shader_program_, "uLightPos");
        const GLint light_color_loc = glGetUniformLocation(tile_shader_program_, "uLightColor");
        const GLint view_pos_loc = glGetUniformLocation(tile_shader_program_, "uViewPos");

        glUniform3f(light_loc, 2.0f, 2.0f, 2.0f);
        glUniform3f(light_color_loc, 1.0f, 1.0f, 1.0f);
        glUniform3f(view_pos_loc, 0.0f, 0.0f, 3.0f);

        // Set tile rendering uniforms (dynamic zoom and atlas info)
        const GLint zoom_loc = glGetUniformLocation(tile_shader_program_, "uZoomLevel");
        const GLint tiles_per_row_loc = glGetUniformLocation(tile_shader_program_, "uTilesPerRow");

        // Get current zoom level from visible tiles (or use default)
        float current_zoom = 2.0f;  // Default
        if (!visible_tiles_.empty()) {
            current_zoom = static_cast<float>(visible_tiles_[0].coordinates.zoom);
        }
        glUniform1f(zoom_loc, current_zoom);
        glUniform1i(tiles_per_row_loc, tiles_per_row_);
        
        // Bind the atlas texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas_texture_);

        const GLint tex_loc = glGetUniformLocation(tile_shader_program_, "uTileTexture");
        glUniform1i(tex_loc, 0);
        
        // Render globe mesh with atlas texture
        glBindVertexArray(globe_vao_);
        glDrawElements(GL_TRIANGLES, globe_indices_.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        
        // Restore previous OpenGL state
        if (!depth_test_enabled) {
            glDisable(GL_DEPTH_TEST);
        }
        if (!cull_face_enabled) {
            glDisable(GL_CULL_FACE);
        }
        
        stats_.rendered_tiles = visible_tiles_.size();
        stats_.texture_binds = 1; // Only one texture bind with atlas
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
    
    std::uint32_t GetGlobeTexture() const override {
        return atlas_texture_;
    }

private:
    TileRenderConfig config_;
    TileManager* tile_manager_ = nullptr;
    bool initialized_ = false;
    std::uint64_t frame_counter_ = 0;
    std::vector<TileRenderState> visible_tiles_;
    TileRenderStats stats_;
    
    // Texture atlas implementation
    int atlas_size_;
    int tile_size_;
    int tiles_per_row_;
    std::uint32_t atlas_texture_ = 0;
    std::vector<AtlasTileInfo> atlas_tiles_;
    std::atomic<bool> atlas_dirty_ = true;
    std::vector<TileCoordinates> last_visible_tiles_;
    
    // OpenGL objects
    std::uint32_t tile_shader_program_ = 0;
    std::uint32_t globe_vao_ = 0;
    std::uint32_t globe_vbo_ = 0;
    std::uint32_t globe_ebo_ = 0;
    std::vector<unsigned int> globe_indices_;
    std::unordered_map<TileCoordinates, std::uint32_t, TileCoordinatesHash> texture_cache_;
    std::uint32_t globe_texture_ = 0;
    bool globe_texture_created_ = false;
    
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
            uniform float uTime;
            uniform float uZoomLevel;      // Current zoom level (passed from CPU)
            uniform int uTilesPerRow;      // Tiles per row in atlas

            // Convert world position to geographic coordinates
            vec3 worldToGeo(vec3 worldPos) {
                float lat = degrees(asin(clamp(worldPos.y, -1.0, 1.0)));  // y is up
                float lon = degrees(atan(worldPos.x, worldPos.z));
                return vec3(lon, lat, 0.0);
            }

            // Convert geographic coordinates to tile coordinates at given zoom level
            vec2 geoToTile(vec2 geo, float zoom) {
                float n = pow(2.0, zoom);
                float x = (geo.x + 180.0) / 360.0 * n;
                // Web Mercator projection with latitude clamping
                float lat_rad = radians(clamp(geo.y, -85.0511, 85.0511));
                float y = (1.0 - log(tan(lat_rad) + 1.0/cos(lat_rad)) / 3.14159265359) / 2.0 * n;
                return vec2(x, y);
            }

            void main() {
                // Basic lighting
                float ambientStrength = 0.25;
                vec3 ambient = ambientStrength * uLightColor;

                vec3 norm = normalize(Normal);
                vec3 lightDir = normalize(uLightPos - FragPos);
                float diff = max(dot(norm, lightDir), 0.0);
                vec3 diffuse = diff * uLightColor;

                // Convert world position to geographic coordinates
                vec3 geo = worldToGeo(normalize(FragPos));

                // Convert to tile coordinates at current zoom level
                float zoom = max(uZoomLevel, 0.0);
                vec2 tile = geoToTile(geo.xy, zoom);

                // Calculate number of tiles at this zoom level
                float numTiles = pow(2.0, zoom);

                // Ensure tile coordinates are in valid range [0, numTiles)
                vec2 tileWrapped = mod(tile, numTiles);

                // Calculate tile fraction (position within the tile)
                vec2 tileFrac = fract(tileWrapped);

                // Map tile coordinates to atlas UV
                // Atlas is organized as a grid of tiles, tiling wraps around
                float tilesPerRowF = float(max(uTilesPerRow, 1));
                float tileSize = 1.0 / tilesPerRowF;

                // Use tile integer coordinates modulo atlas grid size
                vec2 tileInt = floor(tileWrapped);
                vec2 atlasPos = mod(tileInt, vec2(tilesPerRowF));

                // Calculate final UV: atlas position + position within tile
                vec2 atlasUV = (atlasPos + tileFrac) * tileSize;

                // Sample from atlas texture
                vec4 texColor = texture(uTileTexture, atlasUV);

                // Check if texture is valid (not empty/black) - if so, use it
                float texBrightness = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));

                vec3 result;
                if (texBrightness > 0.05) {
                    // Valid texture - use it with lighting
                    result = (ambient + diffuse) * texColor.rgb;
                    FragColor = vec4(result, texColor.a);
                } else {
                    // Placeholder color based on position (ocean blue with variation)
                    vec3 oceanColor = vec3(0.1, 0.25 + 0.1 * sin(geo.x * 0.1), 0.5 + 0.1 * cos(geo.y * 0.1));
                    result = (ambient + diffuse) * oceanColor;
                    FragColor = vec4(result, 1.0);
                }
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
        const int segments = 64;  // Higher resolution for better tile mapping
        const int rings = 32;
        
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
                
                // Texture coordinates (for globe projection - mapping to world coordinates)
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
        
        // Store indices for rendering
        globe_indices_ = indices;
        
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
        
        spdlog::info("Created globe mesh: {} vertices, {} indices", 
                    vertices.size() / 8, indices.size());
        
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
        
        // Draw a simple quad for this tile using modern OpenGL
        const float quad_vertices[] = {
            // positions      // texture coords
            -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
             1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,  1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f,  0.0f, 1.0f
        };
        
        const unsigned int quad_indices[] = {
            0, 1, 2,  // first triangle
            2, 3, 0   // second triangle
        };
        
        // Create temporary VBO and EBO for the quad
        unsigned int temp_vbo, temp_ebo;
        glGenBuffers(1, &temp_vbo);
        glGenBuffers(1, &temp_ebo);
        
        // Bind and fill VBO
        glBindBuffer(GL_ARRAY_BUFFER, temp_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
        
        // Bind and fill EBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, temp_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quad_indices), quad_indices, GL_STATIC_DRAW);
        
        // Set vertex attributes (position + texcoord)
        // Position attribute (location = 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        
        // Texture coordinate attribute (location = 2)
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        
        // Draw the quad
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        
        // Clean up temporary buffers
        glDeleteBuffers(1, &temp_vbo);
        glDeleteBuffers(1, &temp_ebo);
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
        if (atlas_texture_) {
            glDeleteTextures(1, &atlas_texture_);
            atlas_texture_ = 0;
        }
    }
    
    void CalculateAtlasLayout() {
        tiles_per_row_ = atlas_size_ / tile_size_;
        spdlog::info("Atlas layout: {}x{} tiles per row, total {} tiles", 
                    tiles_per_row_, tiles_per_row_, tiles_per_row_ * tiles_per_row_);
    }
    
    void CreateTextureAtlas() {
        if (!atlas_dirty_.load()) {
            return;
        }

        spdlog::info("Creating texture atlas with {} tiles", atlas_tiles_.size());
        
        // Proper atlas creation with error checking
        if (atlas_texture_ == 0) {
            glGenTextures(1, &atlas_texture_);
            
            if (atlas_texture_ == 0) {
                spdlog::error("Failed to generate atlas texture ID");
                atlas_dirty_.store(false);
                return;
            }
            
            glBindTexture(GL_TEXTURE_2D, atlas_texture_);
            
            // Allocate atlas memory
            std::vector<std::uint8_t> empty_data(atlas_size_ * atlas_size_ * 3, 128); // Gray color
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, atlas_size_, atlas_size_, 0, 
                        GL_RGB, GL_UNSIGNED_BYTE, empty_data.data());
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            
            spdlog::info("Created texture atlas {}x{} with ID: {}", 
                        atlas_size_, atlas_size_, atlas_texture_);
        }
        
        // Update atlas with visible tiles
        glBindTexture(GL_TEXTURE_2D, atlas_texture_);
        
        for (size_t i = 0; i < visible_tiles_.size() && i < atlas_tiles_.size(); ++i) {
            const auto& tile = visible_tiles_[i];
            auto& atlas_tile = atlas_tiles_[i];
            
            if (tile.texture_id > 0) {
                // Read tile texture data
                std::vector<std::uint8_t> tile_data(tile_size_ * tile_size_ * 3);
                glBindTexture(GL_TEXTURE_2D, tile.texture_id);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, tile_data.data());
                
                // Calculate atlas position
                int atlas_x = (i % tiles_per_row_) * tile_size_;
                int atlas_y = (i / tiles_per_row_) * tile_size_;
                
                // Update atlas sub-image
                glBindTexture(GL_TEXTURE_2D, atlas_texture_);
                glTexSubImage2D(GL_TEXTURE_2D, 0, atlas_x, atlas_y, 
                               tile_size_, tile_size_, GL_RGB, GL_UNSIGNED_BYTE, tile_data.data());
                
                // Update atlas tile info
                atlas_tile.texture_id = tile.texture_id;
                atlas_tile.u = static_cast<float>(atlas_x) / atlas_size_;
                atlas_tile.v = static_cast<float>(atlas_y) / atlas_size_;
                atlas_tile.u_size = static_cast<float>(tile_size_) / atlas_size_;
                atlas_tile.v_size = static_cast<float>(tile_size_) / atlas_size_;
                atlas_tile.x = tile.coordinates.x;
                atlas_tile.y = tile.coordinates.y;
                atlas_tile.zoom = tile.coordinates.zoom;
            }
        }

        atlas_dirty_.store(false);
        // spdlog::info("Updated texture atlas with {} tiles", visible_tiles_.size());
    }
    
    int CalculateOptimalZoom(float camera_distance) const {
        // Realistic zoom calculation based on camera distance from globe surface
        // Distance from camera to sphere center minus earth radius (1.0f in normalized coords)
        const float altitude = camera_distance - 1.0f;

        // Map altitude to zoom levels (0-18 max for OSM)
        // Camera distance is in normalized units where Earth radius = 1.0
        // altitude ~0.01 = very close (surface), altitude ~2.0 = far (full globe view)

        if (altitude < 0.01f) return 18;   // Extremely close - highest detail
        if (altitude < 0.02f) return 16;
        if (altitude < 0.05f) return 14;
        if (altitude < 0.1f) return 12;
        if (altitude < 0.2f) return 10;
        if (altitude < 0.5f) return 8;
        if (altitude < 1.0f) return 6;
        if (altitude < 2.0f) return 4;
        if (altitude < 5.0f) return 2;
        return 0;  // Very far - lowest detail
    }
    
    BoundingBox2D CalculateVisibleGeographicBounds(const glm::mat4& view_matrix,
                                               const glm::mat4& projection_matrix) const {
        (void)projection_matrix;
        // Calculate realistic visible bounds based on camera position
        // Extract camera position from view matrix
        glm::vec3 camera_pos = glm::vec3(view_matrix[3]);
        float distance = glm::length(camera_pos);
        
        // Calculate visible angle based on field of view
        const float fov_rad = glm::radians(45.0f); // From renderer projection
        const float visible_radius = distance * std::tan(fov_rad / 2.0f);
        
        // Convert visible radius to angular degrees (approximate)
        const float visible_angle_deg = glm::degrees(visible_radius / distance) * 2.0f;
        // const float half_angle = visible_angle_deg / 2.0f;
        
        // Get camera forward direction
        glm::vec3 camera_forward = -glm::normalize(glm::vec3(view_matrix[2]));
        
        // Convert camera position to geographic coordinates (simplified)
        const float lat = glm::degrees(std::asin(-camera_pos.y / distance));
        const float lon = glm::degrees(std::atan2(camera_forward.x, -camera_forward.z));
        
        // Calculate bounds based on visible area around camera
        const float lat_range = std::min(180.0f, visible_angle_deg * 1.5f);
        const float lon_range = std::min(360.0f, visible_angle_deg * 1.5f);
        
        const double min_lat = std::max(-90.0, static_cast<double>(lat - lat_range / 2.0f));
        const double max_lat = std::min(90.0, static_cast<double>(lat + lat_range / 2.0f));
        const double min_lon = static_cast<double>(lon - lon_range / 2.0f);
        const double max_lon = static_cast<double>(lon + lon_range / 2.0f);
        
        spdlog::debug("Camera distance: {:.1f}, visible bounds: lat[ {:.2f}, {:.2f} ] lon[ {:.2f}, {:.2f} ]", 
                    distance, min_lat, max_lat, min_lon, max_lon);
        
        return BoundingBox2D(
            glm::dvec2(min_lon, min_lat),
            glm::dvec2(max_lon, max_lat)
        );

        // Hardcoded full world bounds [ do not remove this ]
        // return BoundingBox2D(
        //     glm::dvec2(-180.0, -85.0511),
        //     glm::dvec2(180.0, 85.0511)
        //     );
    }
    
    bool IsTileInFrustum(const TileCoordinates& tile, const Frustum& frustum) const {
        // Calculate tile's geographic bounds
        const BoundingBox2D bounds = TileMathematics::GetTileBounds(tile);
        const glm::dvec2 center = bounds.GetCenter();

        // Convert tile center to 3D position on unit sphere
        const double lon_rad = glm::radians(center.x);
        const double lat_rad = glm::radians(center.y);

        // Calculate 3D position on unit sphere (Earth radius = 1.0)
        const glm::vec3 tile_center = glm::vec3(
            static_cast<float>(std::cos(lat_rad) * std::sin(lon_rad)),
            static_cast<float>(std::sin(lat_rad)),
            static_cast<float>(std::cos(lat_rad) * std::cos(lon_rad))
        );

        // Estimate tile radius on sphere surface based on zoom level
        // Higher zoom = smaller tiles = smaller bounding sphere
        // At zoom 0, tile covers 180 degrees, at zoom n, tile covers 180/2^n degrees
        const float tile_angular_size = 180.0f / static_cast<float>(1 << tile.zoom);
        const float tile_radius = glm::radians(tile_angular_size) * 0.5f;  // Half the angular size

        // Use frustum sphere intersection test
        return frustum.Intersects(tile_center, tile_radius);
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
        static int texture_counter = 0;
        texture_counter++;
        
        const int texture_size = 256;
        std::vector<std::uint8_t> texture_data;
        texture_data.resize(texture_size * texture_size * 3); // RGB
        
        for (int y = 0; y < texture_size; ++y) {
            for (int x = 0; x < texture_size; ++x) {
                int idx = (y * texture_size + x) * 3;
                
                // Create different colored patterns for different tiles
                int pattern = (x / 32) + (y / 32);
                std::uint8_t r, g, b;
                
                // Use different colors based on texture counter to make them visually distinct
                switch (texture_counter % 4) {
                    case 0: // Red checkerboard
                        r = pattern % 2 ? 255 : 128;
                        g = pattern % 2 ? 0 : 64;
                        b = pattern % 2 ? 0 : 64;
                        break;
                    case 1: // Green checkerboard
                        r = pattern % 2 ? 0 : 64;
                        g = pattern % 2 ? 255 : 128;
                        b = pattern % 2 ? 0 : 64;
                        break;
                    case 2: // Blue checkerboard
                        r = pattern % 2 ? 0 : 64;
                        g = pattern % 2 ? 0 : 64;
                        b = pattern % 2 ? 255 : 128;
                        break;
                    default: // Yellow checkerboard
                        r = pattern % 2 ? 255 : 128;
                        g = pattern % 2 ? 255 : 128;
                        b = pattern % 2 ? 0 : 64;
                        break;
                }
                
                texture_data[idx] = r;
                texture_data[idx + 1] = g;
                texture_data[idx + 2] = b;
            }
        }
        
        // Create OpenGL texture
        std::uint32_t texture_id;
        glGenTextures(1, &texture_id);
        
        // Check for texture generation errors
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            spdlog::error("OpenGL error during glGenTextures in CreateTestTexture: {}", error);
            return 0;
        }
        
        if (texture_id == 0) {
            spdlog::error("Failed to generate test texture - glGenTextures returned 0");
            return 0;
        }
        
        glBindTexture(GL_TEXTURE_2D, texture_id);
        error = glGetError();
        if (error != GL_NO_ERROR) {
            spdlog::error("OpenGL error during glBindTexture in CreateTestTexture: {}", error);
            return 0;
        }
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_size, texture_size, 0, 
                     GL_RGB, GL_UNSIGNED_BYTE, texture_data.data());
        
        error = glGetError();
        if (error != GL_NO_ERROR) {
            spdlog::error("OpenGL error during glTexImage2D in CreateTestTexture: {}", error);
            glDeleteTextures(1, &texture_id);
            return 0;
        }
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        spdlog::info("Created test texture #{} with ID: {}", texture_counter, texture_id);
        return texture_id;
    }
    
    void CreateGlobeTexture() {
        // Create globe texture using test pattern
        // In a full implementation, this would blend/combine visible tiles
        const int texture_size = 1024;  // Higher resolution for globe
        std::vector<std::uint8_t> texture_data;
        texture_data.resize(texture_size * texture_size * 3); // RGB
        
        // Create a simple world map pattern
        for (int y = 0; y < texture_size; ++y) {
            for (int x = 0; x < texture_size; ++x) {
                int idx = (y * texture_size + x) * 3;
                
                // Create latitude-based color bands
                float lat_norm = static_cast<float>(y) / texture_size;
                float lon_norm = static_cast<float>(x) / texture_size;
                
                // Simulate continents (simplified)
                bool is_land = false;
                if (lat_norm > 0.3f && lat_norm < 0.7f) {
                    if (lon_norm > 0.2f && lon_norm < 0.8f) {
                        is_land = true;
                    }
                }
                
                // Create more realistic pattern
                if (is_land) {
                    texture_data[idx] = 34;     // R - greenish
                    texture_data[idx + 1] = 139;  // G  
                    texture_data[idx + 2] = 34;   // B
                } else {
                    texture_data[idx] = 70;      // R - bluish
                    texture_data[idx + 1] = 130;  // G  
                    texture_data[idx + 2] = 180;  // B
                }
            }
        }
        
        // Create OpenGL texture
        glGenTextures(1, &globe_texture_);
        glBindTexture(GL_TEXTURE_2D, globe_texture_);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_size, texture_size, 0, 
                     GL_RGB, GL_UNSIGNED_BYTE, texture_data.data());
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        globe_texture_created_ = true;
        spdlog::info("Created globe texture with ID: {}", globe_texture_);
    }
    
    void RenderTileOnGlobe(const TileRenderState& tile, 
                          const glm::mat4& view_matrix,
                          const glm::mat4& projection_matrix) {
        (void)view_matrix;
        (void)projection_matrix;
        // Calculate tile position on globe surface using geographic coordinates
        auto bounds = tile.geographic_bounds;
        auto center = bounds.GetCenter();
        
        // Convert geographic coordinates to 3D position on sphere
        double lon = center.x;
        double lat = center.y;
        
        // Convert to radians
        double lon_rad = lon * M_PI / 180.0;
        double lat_rad = lat * M_PI / 180.0;
        
        // Calculate 3D position on unit sphere
        glm::vec3 tile_pos = glm::vec3(
            std::cos(lat_rad) * std::sin(lon_rad),  // x
            std::sin(lat_rad),                      // y  
            std::cos(lat_rad) * std::cos(lon_rad)   // z
        );
        
        // Scale tile to cover appropriate area on globe surface
        // Calculate tile size in radians
        double lat_span = (bounds.max.y - bounds.min.y) * M_PI / 180.0;
        double lon_span = (bounds.max.x - bounds.min.x) * M_PI / 180.0;
        
        // Use arc length approximation for tile size
        float tile_size = static_cast<float>(std::max(lat_span, lon_span)) * 0.5f;
        
        // Create model matrix for this tile
        glm::mat4 model = glm::translate(glm::mat4(1.0f), tile_pos);
        
        // Scale tile to appropriate size on globe surface
        model = glm::scale(model, glm::vec3(tile_size));
        
        // Orient tile to face outward from globe center (align with normal)
        glm::vec3 normal = glm::normalize(tile_pos);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::cross(up, normal);
        glm::vec3 tile_up = glm::cross(normal, right);
        
        glm::mat4 rotation = glm::mat4(1.0f);
        rotation[0] = glm::vec4(right, 0.0f);
        rotation[1] = glm::vec4(tile_up, 0.0f); 
        rotation[2] = glm::vec4(normal, 0.0f);
        rotation[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        
        model = model * rotation;
        
        // Set uniforms for this tile
        const GLint model_loc = glGetUniformLocation(tile_shader_program_, "uModel");
        glUniformMatrix4fv(model_loc, 1, GL_FALSE, glm::value_ptr(model));
        
        // Bind tile texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tile.texture_id);
        
        // Set texture uniform
        const GLint tex_loc = glGetUniformLocation(tile_shader_program_, "uTileTexture");
        glUniform1i(tex_loc, 0);
        
        // Draw tile quad on globe surface
        const float quad_vertices[] = {
            // positions      // texture coords
            -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
             0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
             0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f, 0.0f, 1.0f
        };
        
        const unsigned int quad_indices[] = {
            0, 1, 2,  // first triangle
            2, 3, 0   // second triangle
        };
        
        // Create temporary VBO and EBO for tile quad
        unsigned int temp_vbo, temp_ebo;
        glGenBuffers(1, &temp_vbo);
        glGenBuffers(1, &temp_ebo);
        
        // Bind and fill VBO
        glBindBuffer(GL_ARRAY_BUFFER, temp_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
        
        // Bind and fill EBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, temp_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quad_indices), quad_indices, GL_STATIC_DRAW);
        
        // Set vertex attributes (position + texcoord)
        // Position attribute (location = 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        
        // Texture coordinate attribute (location = 2)
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        
        // Draw tile quad
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        
        // Clean up temporary buffers
        glDeleteBuffers(1, &temp_vbo);
        glDeleteBuffers(1, &temp_ebo);
    }
    
    void TriggerTileLoading(const TileCoordinates& coords) {
        // Trigger async tile texture loading through tile manager
        if (tile_manager_) {
            // Define callback to handle texture loading completion
            auto texture_loaded_callback = [this](const TileCoordinates& loaded_coords, std::uint32_t texture_id) {
                // Texture loaded successfully, mark atlas as dirty so it gets updated with the new texture
                atlas_dirty_.store(true);
                spdlog::info("Tile texture loaded and atlas marked dirty for {}/{}/{}: texture_id={}",
                             loaded_coords.x, loaded_coords.y, loaded_coords.zoom, texture_id);
            };

            auto future = tile_manager_->LoadTileTextureAsync(coords, texture_loaded_callback);
            spdlog::info("Triggered async tile texture loading for {}/{}/{}", coords.x, coords.y, coords.zoom);
        } else {
            spdlog::warn("No tile manager available for loading tiles");
        }
    }
};

// Factory function
std::unique_ptr<TileRenderer> TileRenderer::Create(const TileRenderConfig& config) {
    return std::make_unique<TileRendererImpl>(config);
}

} // namespace earth_map
