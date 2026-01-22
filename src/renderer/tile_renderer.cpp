/**
 * @file tile_renderer.cpp
 * @brief Tile rendering system implementation
 */

#include <earth_map/renderer/tile_renderer.h>
#include <earth_map/renderer/globe_mesh.h>
#include <earth_map/math/coordinate_system.h>
#include <earth_map/math/projection.h>
#include <earth_map/math/tile_mathematics.h>
#include <earth_map/renderer/texture_atlas/tile_texture_coordinator.h>
#include <spdlog/spdlog.h>
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <unordered_map>
#include <climits>
#include <cstddef>
#include <iostream>
#include <thread>

namespace earth_map {

/**
 * @brief Tile rendering state
 */
struct TileRenderState {
    TileCoordinates coordinates;        ///< Tile coordinates (x, y, zoom)
    glm::vec4 uv_coords;            ///< Atlas UV coordinates (u_min, v_min, u_max, v_max)
    bool is_ready;                   ///< Whether tile texture is ready in atlas
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
        : config_(config), frame_counter_(0), atlas_size_(2048), tile_size_(256) {
        spdlog::info("Creating tile renderer with max tiles: {}", config.max_visible_tiles);
        // Calculate tiles per row for atlas layout
        tiles_per_row_ = atlas_size_ / tile_size_;
        spdlog::info("Atlas layout: {}x{} tiles per row, total {} tiles",
                    tiles_per_row_, tiles_per_row_, tiles_per_row_ * tiles_per_row_);
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

        // Process GL uploads from worker threads (must be on GL thread)
        if (texture_coordinator_) {
            texture_coordinator_->ProcessUploads(5);  // Upload up to 5 tiles per frame for 60 FPS
        }

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

    void SetTextureCoordinator(TileTextureCoordinator* coordinator) override {
        texture_coordinator_ = coordinator;
        spdlog::info("Tile renderer: texture coordinator set");
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
            camera_position, view_matrix, projection_matrix);

        // CRITICAL DEBUG: Log bounds to detect inversion
        static int log_counter = 0;
        if (++log_counter % 60 == 0) {
            spdlog::warn("=== TILE VISIBILITY DEBUG ===");
            spdlog::warn("Camera position: ({:.2f}, {:.2f}, {:.2f})",
                camera_position.x, camera_position.y, camera_position.z);
            glm::vec3 look_dir = -glm::normalize(camera_position);
            float look_lat = glm::degrees(std::asin(std::clamp(look_dir.y, -1.0f, 1.0f)));
            float look_lon = glm::degrees(std::atan2(look_dir.x, look_dir.z));
            spdlog::warn("Camera looks at: lon={:.1f}°, lat={:.1f}°", look_lon, look_lat);
            spdlog::warn("Visible bounds: lon[{:.1f}, {:.1f}], lat[{:.1f}, {:.1f}]",
                visible_bounds.min.x, visible_bounds.max.x,
                visible_bounds.min.y, visible_bounds.max.y);
        }

        // Get tiles in visible bounds at appropriate zoom
        const std::vector<TileCoordinates> candidate_tiles =
            tile_manager_->GetTilesInBounds(visible_bounds, zoom_level);

        // Log tile count for debugging
        if (log_counter % 60 == 0) {
            spdlog::warn("Candidate tiles: {}, Zoom: {}", candidate_tiles.size(), zoom_level);
            if (!candidate_tiles.empty()) {
                // Show representative tiles from different x-columns to verify longitude distribution
                std::map<int32_t, size_t> x_column_first_index;
                for (size_t i = 0; i < candidate_tiles.size(); ++i) {
                    int32_t x = candidate_tiles[i].x;
                    if (x_column_first_index.find(x) == x_column_first_index.end()) {
                        x_column_first_index[x] = i;
                    }
                }

                spdlog::warn("Representative tiles (one per x-column):");
                for (const auto& [x_val, idx] : x_column_first_index) {
                    auto bounds = TileMathematics::GetTileBounds(candidate_tiles[idx]);
                    spdlog::warn("  Tile {} at x={}: ({},{},z{}) -> lon[{:.1f},{:.1f}], lat[{:.1f},{:.1f}]",
                        idx, x_val,
                        candidate_tiles[idx].x, candidate_tiles[idx].y, candidate_tiles[idx].zoom,
                        bounds.min.x, bounds.max.x, bounds.min.y, bounds.max.y);
                }
            }
        }
        
        // Filter tiles by frustum culling and limit with performance considerations
        std::size_t tiles_added = 0;
        std::size_t max_tiles_for_frame = std::min(static_cast<std::size_t>(config_.max_visible_tiles),
                                                 static_cast<std::size_t>(tiles_per_row_ * tiles_per_row_));

        // Collect visible tile coordinates and request them from texture coordinator
        std::vector<TileCoordinates> visible_tile_coords;
        for (const TileCoordinates& tile_coords : candidate_tiles) {
            if (tiles_added >= max_tiles_for_frame) {
                break;
            }

            // Check if tile is in frustum
            if (IsTileInFrustum(tile_coords, frustum)) {
                visible_tile_coords.push_back(tile_coords);
                tiles_added++;
            }
        }

        // Request all visible tiles from texture coordinator (idempotent, lock-free)
        if (texture_coordinator_ && !visible_tile_coords.empty()) {
            // Calculate priority based on camera distance (closer = lower number = higher priority)
            int priority = static_cast<int>(camera_distance * 10.0f);
            texture_coordinator_->RequestTiles(visible_tile_coords, priority);
        }

        // Build visible tiles list with UV coords from coordinator
        for (const TileCoordinates& tile_coords : visible_tile_coords) {
                TileRenderState tile_state;
                tile_state.coordinates = tile_coords;
                tile_state.geographic_bounds = TileMathematics::GetTileBounds(tile_coords);
                tile_state.lod_level = CalculateTileLOD(tile_coords, camera_distance);
                tile_state.last_used = static_cast<float>(frame_counter_);
                tile_state.load_priority = CalculateLoadPriority(tile_coords, camera_position);
                tile_state.is_visible = true;

                // Get UV coordinates and ready state from coordinator
                if (texture_coordinator_) {
                    tile_state.uv_coords = texture_coordinator_->GetTileUV(tile_coords);
                    tile_state.is_ready = texture_coordinator_->IsTileReady(tile_coords);
                } else {
                    // Default UV coords (full texture)
                    tile_state.uv_coords = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
                    tile_state.is_ready = false;
                }

                visible_tiles_.push_back(tile_state);
        }
        
        // spdlog::info("Selected {} tiles for rendering (zoom {})", tiles_added, zoom_level);
        
        // Sort tiles by priority (highest first)
        std::sort(visible_tiles_.begin(), visible_tiles_.end(),
                 [](const TileRenderState& a, const TileRenderState& b) {
                     return a.load_priority < b.load_priority;
                 });

        // Track visible tiles for change detection
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

        // Set tile rendering uniforms (dynamic zoom)
        const GLint zoom_loc = glGetUniformLocation(tile_shader_program_, "uZoomLevel");

        // Get current zoom level from visible tiles (or use default)
        float current_zoom = 2.0f;  // Default
        if (!visible_tiles_.empty()) {
            current_zoom = static_cast<float>(visible_tiles_[0].coordinates.zoom);
        }
        glUniform1f(zoom_loc, current_zoom);

        // Populate tile data arrays for shader (only ready tiles)
        constexpr int MAX_SHADER_TILES = 256;
        std::vector<GLint> tile_coords_data;  // Flat array: x0,y0,z0, x1,y1,z1, ...
        std::vector<GLfloat> tile_uvs_data;   // Flat array: u0,v0,w0,h0, u1,v1,w1,h1, ...

        tile_coords_data.reserve(visible_tiles_.size() * 3);
        tile_uvs_data.reserve(visible_tiles_.size() * 4);

        int num_shader_tiles = 0;
        for (const auto& tile : visible_tiles_) {
            if (num_shader_tiles >= MAX_SHADER_TILES) break;
            if (!tile.is_ready) continue;  // Only send ready tiles to shader

            // Add tile coordinates
            tile_coords_data.push_back(static_cast<GLint>(tile.coordinates.x));
            tile_coords_data.push_back(static_cast<GLint>(tile.coordinates.y));
            tile_coords_data.push_back(static_cast<GLint>(tile.coordinates.zoom));

            // Add tile UV coordinates
            tile_uvs_data.push_back(tile.uv_coords.x);  // u_min
            tile_uvs_data.push_back(tile.uv_coords.y);  // v_min
            tile_uvs_data.push_back(tile.uv_coords.z);  // u_max
            tile_uvs_data.push_back(tile.uv_coords.w);  // v_max

            num_shader_tiles++;
        }

        // Set tile data uniforms
        const GLint num_tiles_loc = glGetUniformLocation(tile_shader_program_, "uNumTiles");
        const GLint tile_coords_loc = glGetUniformLocation(tile_shader_program_, "uTileCoords");
        const GLint tile_uvs_loc = glGetUniformLocation(tile_shader_program_, "uTileUVs");

        glUniform1i(num_tiles_loc, num_shader_tiles);

        if (num_shader_tiles > 0) {
            glUniform3iv(tile_coords_loc, num_shader_tiles, tile_coords_data.data());
            glUniform4fv(tile_uvs_loc, num_shader_tiles, tile_uvs_data.data());
        }

        // Bind the coordinator's atlas texture
        glActiveTexture(GL_TEXTURE0);
        std::uint32_t atlas_texture_id = 0;
        if (texture_coordinator_) {
            atlas_texture_id = texture_coordinator_->GetAtlasTextureID();
        }
        glBindTexture(GL_TEXTURE_2D, atlas_texture_id);

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
        if (texture_coordinator_) {
            return texture_coordinator_->GetAtlasTextureID();
        }
        return 0;
    }

private:
    TileRenderConfig config_;
    TileManager* tile_manager_ = nullptr;
    TileTextureCoordinator* texture_coordinator_ = nullptr;
    bool initialized_ = false;
    std::uint64_t frame_counter_ = 0;
    std::vector<TileRenderState> visible_tiles_;
    TileRenderStats stats_;
    
    // Texture atlas implementation
    int atlas_size_;
    int tile_size_;
    int tiles_per_row_;
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
            uniform float uZoomLevel;      // Current zoom level

            // Tile data from TileTextureCoordinator
            #define MAX_TILES 256
            uniform int uNumTiles;                    // Number of visible tiles
            uniform ivec3 uTileCoords[MAX_TILES];     // Tile coordinates (x, y, zoom)
            uniform vec4 uTileUVs[MAX_TILES];         // Atlas UV coords (u_min, v_min, u_max, v_max)

            // Convert world position to geographic coordinates
            vec3 worldToGeo(vec3 worldPos) {
                float lat = degrees(asin(clamp(worldPos.y, -1.0, 1.0)));
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

            // Find tile UV coordinates from loaded tiles
            vec4 findTileUV(ivec3 tileCoord, vec2 tileFrac) {
                // Search for matching tile in loaded tiles
                for (int i = 0; i < uNumTiles && i < MAX_TILES; i++) {
                    if (uTileCoords[i] == tileCoord) {
                        // Found the tile - interpolate within its UV region
                        vec4 uv = uTileUVs[i];
                        vec2 atlasUV = mix(uv.xy, uv.zw, tileFrac);
                        return vec4(atlasUV, 1.0, 1.0);  // Return UV + found flag
                    }
                }
                return vec4(0.0, 0.0, 0.0, 0.0);  // Not found
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

                // Get integer tile coordinates
                ivec2 tileInt = ivec2(floor(tile));
                int zoomInt = int(zoom);

                // Calculate tile fraction (position within the tile)
                vec2 tileFrac = fract(tile);

                // Look up tile UV from coordinator's data
                ivec3 tileCoord = ivec3(tileInt, zoomInt);
                vec4 uvResult = findTileUV(tileCoord, tileFrac);

                vec3 result;
                if (uvResult.z > 0.5) {
                    // Tile found - sample from atlas using coordinator's UV
                    vec4 texColor = texture(uTileTexture, uvResult.xy);

                    float texBrightness = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
                    if (texBrightness > 0.05) {
                        // Valid texture data
                        result = (ambient + diffuse) * texColor.rgb;
                        FragColor = vec4(result, texColor.a);
                    } else {
                        // Tile loaded but empty - placeholder
                        vec3 oceanColor = vec3(0.1, 0.3, 0.5);
                        result = (ambient + diffuse) * oceanColor;
                        FragColor = vec4(result, 1.0);
                    }
                } else {
                    // Tile not loaded yet - show placeholder (darker blue)
                    vec3 oceanColor = vec3(0.05, 0.15 + 0.05 * sin(geo.x * 0.1), 0.25 + 0.05 * cos(geo.y * 0.1));
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
                // CRITICAL: Use sin(phi) for X and cos(phi) for Z to match shader's lon=0 → +Z convention
                const float x = radius * sin_theta * sin_phi;
                const float y = radius * cos_theta;
                const float z = radius * sin_theta * cos_phi;

                // Normal (same as position for sphere)
                const float nx = sin_theta * sin_phi;
                const float ny = cos_theta;
                const float nz = sin_theta * cos_phi;
                
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
        // For now, skip tiles that are not ready
        if (!tile.is_ready) {
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

        // Bind coordinator's atlas texture
        glActiveTexture(GL_TEXTURE0);
        std::uint32_t atlas_texture_id = 0;
        if (texture_coordinator_) {
            atlas_texture_id = texture_coordinator_->GetAtlasTextureID();
        }
        glBindTexture(GL_TEXTURE_2D, atlas_texture_id);
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
    }
    
    int CalculateOptimalZoom(float camera_distance) const {
        // Realistic zoom calculation based on camera distance from globe surface
        // Distance from camera to sphere center minus earth radius (1.0f in normalized coords)
        const float altitude = camera_distance - 1.0f;

        // Map altitude to zoom levels (0-18 max for OSM)
        // Camera distance is in normalized units where Earth radius = 1.0
        // altitude ~0.01 = very close (surface), altitude ~2.0 = far (full globe view)

        // Logarithmic mapping: lower altitude = higher zoom
        // altitude = 0.01 -> zoom = 10 (close up)
        // altitude = 2.0 -> zoom = 2 (full globe)
        // altitude = 10.0 -> zoom = 0 (very far)

        // DO NOT REMOVE THIS. IT AFFECTS NOTHING, THE ENTIRE EARTH WILL BE RENREDER AT ZOOM LEVEL 2 with just 16 TILES. IT IS NEEDED TO NOT OVERLOAD NETWORK
        return 2;

        if (altitude <= 0.0f) {
            return 10;  // Maximum zoom when on/below surface
        }

        // Use logarithmic scale: zoom = max_zoom - log2(altitude + 1) * scale_factor
        const float max_zoom = 10.0f;
        const float scale_factor = 3.0f;  // Adjust to tune zoom sensitivity
        int zoom = static_cast<int>(max_zoom - std::log2(altitude + 1.0f) * scale_factor);

        // Clamp to valid range [0, 10]
        return std::clamp(zoom, 0, 10);
    }
    
    BoundingBox2D CalculateVisibleGeographicBounds(const glm::vec3& camera_position,
                                               const glm::mat4& view_matrix,
                                               const glm::mat4& projection_matrix) const {
        (void)projection_matrix;
        (void)view_matrix;

        // Use the camera position directly (in world coordinates)
        float distance = glm::length(camera_position);

        // CRITICAL FIX: Calculate where camera is LOOKING, not where it IS
        // Camera is positioned OUTSIDE globe looking TOWARDS origin (0,0,0)
        // The look direction is the negative of the position (pointing inward)
        glm::vec3 look_direction = -glm::normalize(camera_position);

        // Convert look direction to geographic coordinates
        // This gives us the point on the sphere surface where camera is looking
        // lat = asin(y), lon = atan2(x, z)
        const float lat = glm::degrees(std::asin(std::clamp(look_direction.y, -1.0f, 1.0f)));
        const float lon = glm::degrees(std::atan2(look_direction.x, look_direction.z));

        // PROPER SPHERICAL VISIBILITY CALCULATION
        // For a camera looking at a sphere from outside:
        // - Globe radius R = 1.0 (normalized)
        // - Camera distance D = distance
        // - The horizon forms a cone around the look direction
        const float globe_radius = 1.0f;

        // Calculate the angular radius of the visible horizon from camera
        // This is the half-angle of the cone that just touches the sphere
        const float horizon_angle_rad = std::asin(globe_radius / distance);
        const float horizon_angle_deg = glm::degrees(horizon_angle_rad);

        // Calculate FOV contribution
        const float fov_deg = 45.0f;  // Camera FOV
        const float fov_half = fov_deg / 2.0f;

        // The visible angular extent is the combination of:
        // 1. The horizon angle (geometric visibility on sphere)
        // 2. The FOV cone (what fits in the camera view)
        // Use the larger of the two, plus a margin for safety
        const float base_visible_angle = std::max(horizon_angle_deg, fov_half);

        // For GIS applications, use generous coverage to ensure smooth experience
        // At typical viewing distances (2-5 units), we want near-hemisphere coverage
        float lat_range, lon_range;

        if (distance <= 5.0f) {
            // Close/medium view: Load full hemisphere plus margin
            // This ensures tiles are always available as user rotates
            lat_range = 160.0f;  // Nearly full hemisphere (±80° from center)
            lon_range = 160.0f;
            spdlog::debug("Using hemisphere coverage mode: {:.0f}° x {:.0f}°", lat_range, lon_range);
        } else {
            // Far view: Calculate based on actual visibility
            const float margin_multiplier = 2.0f;  // 100% extra coverage for far views
            const float visible_angle_with_margin = base_visible_angle * margin_multiplier;
            lat_range = std::min(170.0f, visible_angle_with_margin * 2.0f);
            lon_range = std::min(360.0f, visible_angle_with_margin * 2.0f);
            spdlog::debug("Using calculated coverage: {:.0f}° x {:.0f}°", lat_range, lon_range);
        }

        spdlog::debug("Visibility calc: distance={:.2f}, horizon_angle={:.1f}°, "
                     "fov_half={:.1f}°, base_visible={:.1f}°",
                     distance, horizon_angle_deg, fov_half, base_visible_angle);

        // Clamp to Web Mercator valid bounds (-85.0511 to 85.0511)
        const double min_lat = std::max(-85.0, static_cast<double>(lat - lat_range / 2.0f));
        const double max_lat = std::min(85.0, static_cast<double>(lat + lat_range / 2.0f));

        // Calculate longitude bounds
        double min_lon = static_cast<double>(lon - lon_range / 2.0f);
        double max_lon = static_cast<double>(lon + lon_range / 2.0f);

        // CRITICAL FIX: Handle date line wraparound
        // If bounds exceed [-180, 180], we're crossing the date line
        // Instead of clamping (which loses coverage), expand to full longitude range
        bool crosses_dateline = (min_lon < -180.0 || max_lon > 180.0);

        if (crosses_dateline) {
            // View crosses date line - request full longitude range to ensure coverage
            // This is acceptable at typical viewing distances where we see ~hemisphere
            spdlog::debug("Date line crossing detected, using full longitude range");
            min_lon = -180.0;
            max_lon = 180.0;
        } else {
            // Normal case - just clamp to valid range
            min_lon = std::max(-180.0, std::min(180.0, min_lon));
            max_lon = std::max(-180.0, std::min(180.0, max_lon));
        }

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

        // Check if OpenGL context is available before calling any OpenGL functions
        // In headless mode or during context issues, gracefully handle the situation
        auto gl_version = glGetString(GL_VERSION);
        if (gl_version == nullptr) {
            spdlog::error("OpenGL context not available in CreateTestTexture - skipping texture creation");
            return 0;  // Return 0 instead of crashing
        }
        
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
        
        // spdlog::info("Created test texture #{} with ID: {}", texture_counter, texture_id);
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

        // Bind coordinator's atlas texture
        glActiveTexture(GL_TEXTURE0);
        std::uint32_t atlas_texture_id = 0;
        if (texture_coordinator_) {
            atlas_texture_id = texture_coordinator_->GetAtlasTextureID();
        }
        glBindTexture(GL_TEXTURE_2D, atlas_texture_id);

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
    
};
// Note: TriggerTileLoading() removed - tile loading now handled by TileTextureCoordinator

// Factory function
std::unique_ptr<TileRenderer> TileRenderer::Create(const TileRenderConfig& config) {
    return std::make_unique<TileRendererImpl>(config);
}

} // namespace earth_map
