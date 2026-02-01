/**
 * @file tile_renderer.cpp
 * @brief Tile rendering system implementation
 */

#include <earth_map/renderer/tile_renderer.h>
#include <earth_map/renderer/globe_mesh.h>
#include <earth_map/renderer/shader_loader.h>
#include <earth_map/math/projection.h>
#include <earth_map/math/tile_mathematics.h>
#include <earth_map/renderer/texture_atlas/tile_texture_coordinator.h>
#include <earth_map/coordinates/coordinate_mapper.h>
#include <earth_map/constants.h>
#include <spdlog/spdlog.h>
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <cstddef>

namespace earth_map {

namespace {

constexpr int kDefaultAtlasSize = 2048;
constexpr int kDefaultTileSize = 256;
constexpr int kDefaultZoomLevel = 2;
constexpr int kMaxShaderTiles = 256;
constexpr glm::vec3 kDefaultLightPosition{2.0f, 2.0f, 2.0f};
constexpr glm::vec3 kDefaultViewPosition{0.0f, 0.0f, 3.0f};

} // namespace

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
        : config_(config), frame_counter_(0), atlas_size_(kDefaultAtlasSize), tile_size_(kDefaultTileSize) {
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

    void SetGlobeMesh(GlobeMesh* globe_mesh) override {
        if (!globe_mesh) {
            spdlog::error("Tile renderer: cannot set null globe mesh");
            return;
        }

        globe_mesh_ = globe_mesh;
        mesh_uploaded_to_gpu_ = false;  // Mark for re-upload

        spdlog::info("Tile renderer: globe mesh set ({} vertices, {} triangles)",
                     globe_mesh_->GetVertices().size(),
                     globe_mesh_->GetTriangles().size());
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

        // Get tiles in visible bounds at appropriate zoom
        const std::vector<TileCoordinates> candidate_tiles =
            tile_manager_->GetTilesInBounds(visible_bounds, zoom_level);
        
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
        if (!initialized_) {
            return;
        }

        // CRITICAL: Must have globe mesh to render on
        if (!globe_mesh_) {
            spdlog::warn("Tile renderer: no globe mesh set, cannot render tiles");
            return;
        }

        // Upload mesh to GPU if not yet done or if mesh changed
        if (!mesh_uploaded_to_gpu_) {
            if (!UploadMeshToGPU()) {
                spdlog::error("Tile renderer: failed to upload mesh to GPU");
                return;
            }
        }

        // If no visible tiles, render with base color
        // (Don't skip rendering - globe should always be visible)

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

        glUniform3f(light_loc, kDefaultLightPosition.x, kDefaultLightPosition.y, kDefaultLightPosition.z);
        glUniform3f(light_color_loc, 1.0f, 1.0f, 1.0f);
        glUniform3f(view_pos_loc, kDefaultViewPosition.x, kDefaultViewPosition.y, kDefaultViewPosition.z);

        // Set tile rendering uniforms (dynamic zoom)
        const GLint zoom_loc = glGetUniformLocation(tile_shader_program_, "uZoomLevel");

        // Get current zoom level from visible tiles (or use default)
        int current_zoom = kDefaultZoomLevel;
        if (!visible_tiles_.empty()) {
            current_zoom = visible_tiles_[0].coordinates.zoom;
        }
        glUniform1i(zoom_loc, current_zoom);

        // Populate tile data arrays for shader (only ready tiles)
        constexpr int MAX_SHADER_TILES = kMaxShaderTiles;
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
    
    TileCoordinates GetTileAtScreenCoords(float /*screen_x*/,
                                          float /*screen_y*/,
                                          std::uint32_t /*screen_width*/,
                                          std::uint32_t /*screen_height*/,
                                          const glm::mat4& /*view_matrix*/,
                                          const glm::mat4& /*projection_matrix*/) override {
        throw std::runtime_error("GetTileAtScreenCoords not implemented");
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
    GlobeMesh* globe_mesh_ = nullptr;  // External globe mesh to render on
    bool initialized_ = false;
    bool mesh_uploaded_to_gpu_ = false;  // Track if mesh data is on GPU
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
    
    // Tile atlas vertex shader source
    // Canonical source: src/shaders/tile_atlas.vert
    static constexpr const char* kTileVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

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

    // Tile atlas fragment shader source
    // Canonical source: src/shaders/tile_atlas.frag
    static constexpr const char* kTileFragmentShader = R"(
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
uniform int uZoomLevel;

#define MAX_TILES 256
uniform int uNumTiles;
uniform ivec3 uTileCoords[MAX_TILES];
uniform vec4 uTileUVs[MAX_TILES];

vec2 worldToGeo(vec3 pos) {
    vec3 normalized = normalize(pos);
    float lat = asin(normalized.y) * 180.0 / 3.14159265359;
    float lon = atan(normalized.x, normalized.z) * 180.0 / 3.14159265359;
    return vec2(lon, lat);
}

// Web Mercator is intentional: matches standard XYZ tile server layout (OSM, etc.)
ivec2 geoToTile(vec2 geo, int zoom) {
    const float PI = 3.14159265359;
    int n = 1 << zoom;
    float norm_lon = (geo.x + 180.0) / 360.0;
    float lat_clamped = clamp(geo.y, -85.0511, 85.0511);
    float lat_rad = lat_clamped * PI / 180.0;
    float merc_y = log(tan(PI / 4.0 + lat_rad / 2.0));
    float norm_lat = (1.0 - merc_y / PI) / 2.0;
    int tile_x = clamp(int(floor(norm_lon * float(n))), 0, n - 1);
    int tile_y = clamp(int(floor(norm_lat * float(n))), 0, n - 1);
    return ivec2(tile_x, tile_y);
}

vec2 getTileFrac(vec2 geo, ivec2 tile, int zoom) {
    const float PI = 3.14159265359;
    int n = 1 << zoom;
    float norm_lon = (geo.x + 180.0) / 360.0;
    float lat_clamped = clamp(geo.y, -85.0511, 85.0511);
    float lat_rad = lat_clamped * PI / 180.0;
    float merc_y = log(tan(PI / 4.0 + lat_rad / 2.0));
    float norm_lat = (1.0 - merc_y / PI) / 2.0;
    return vec2(fract(norm_lon * float(n)), fract(norm_lat * float(n)));
}

vec4 findTileUV(vec2 geo, int zoom) {
    ivec2 tile = geoToTile(geo, zoom);
    vec2 tileFrac = getTileFrac(geo, tile, zoom);
    ivec3 tileCoord = ivec3(tile, zoom);
    for (int i = 0; i < uNumTiles && i < MAX_TILES; i++) {
        if (uTileCoords[i] == tileCoord) {
            vec4 uv = uTileUVs[i];
            vec2 atlasUV = mix(uv.xy, uv.zw, tileFrac);
            return vec4(atlasUV, 1.0, 1.0);
        }
    }
    return vec4(0.0, 0.0, 0.0, 0.0);
}

void main() {
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * uLightColor;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;

    vec2 geo = worldToGeo(FragPos);
    int zoom = max(uZoomLevel, 0);
    vec4 uvResult = findTileUV(geo, zoom);

    vec3 result;
    if (uvResult.z > 0.5) {
        vec4 texColor = texture(uTileTexture, uvResult.xy);
        float texBrightness = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (texBrightness > 0.05) {
            result = (ambient + diffuse) * texColor.rgb;
            FragColor = vec4(result, texColor.a);
        } else {
            vec3 baseColor = vec3(0.85, 0.82, 0.75);
            result = (ambient + diffuse) * baseColor;
            FragColor = vec4(result, 1.0);
        }
    } else {
        vec3 baseColor = vec3(0.85, 0.82, 0.75);
        result = (ambient + diffuse) * baseColor;
        FragColor = vec4(result, 1.0);
    }
}
)";

    bool InitializeOpenGLState() {
        tile_shader_program_ = ShaderLoader::CreateProgram(
            kTileVertexShader, kTileFragmentShader, "tile_atlas");

        if (tile_shader_program_ == 0) {
            spdlog::error("Failed to create tile atlas shader program");
            return false;
        }

        spdlog::info("Tile renderer OpenGL state initialized (mesh will be uploaded when provided)");
        return true;
    }
    
    bool UploadMeshToGPU() {
        // Upload the provided icosahedron mesh to GPU
        // This replaces the old sphere generation - we now use the actual displaced globe mesh

        if (!globe_mesh_) {
            spdlog::error("UploadMeshToGPU: no globe mesh available");
            return false;
        }

        const auto& mesh_vertices = globe_mesh_->GetVertices();
        const auto& mesh_indices = globe_mesh_->GetVertexIndices();

        if (mesh_vertices.empty() || mesh_indices.empty()) {
            spdlog::error("UploadMeshToGPU: globe mesh has no geometry");
            return false;
        }

        spdlog::info("Uploading globe mesh to GPU: {} vertices, {} indices",
                     mesh_vertices.size(), mesh_indices.size());

        // Convert GlobeVertex to flat array for OpenGL
        // Format: position(3) + normal(3) + texcoord(2) = 8 floats per vertex
        std::vector<float> vertices;
        vertices.reserve(mesh_vertices.size() * 8);

        for (const auto& vertex : mesh_vertices) {
            // Position
            vertices.push_back(vertex.position.x);
            vertices.push_back(vertex.position.y);
            vertices.push_back(vertex.position.z);
            // Normal
            vertices.push_back(vertex.normal.x);
            vertices.push_back(vertex.normal.y);
            vertices.push_back(vertex.normal.z);
            // Texture coordinates
            vertices.push_back(vertex.texcoord.x);
            vertices.push_back(vertex.texcoord.y);
        }

        // Store indices for rendering
        globe_indices_.clear();
        globe_indices_.reserve(mesh_indices.size());
        for (const auto& index : mesh_indices) {
            globe_indices_.push_back(static_cast<unsigned int>(index));
        }

        // Clean up old GPU resources if they exist
        if (globe_vao_ != 0) {
            glDeleteVertexArrays(1, &globe_vao_);
            globe_vao_ = 0;
        }
        if (globe_vbo_ != 0) {
            glDeleteBuffers(1, &globe_vbo_);
            globe_vbo_ = 0;
        }
        if (globe_ebo_ != 0) {
            glDeleteBuffers(1, &globe_ebo_);
            globe_ebo_ = 0;
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
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, globe_indices_.size() * sizeof(unsigned int),
                    globe_indices_.data(), GL_STATIC_DRAW);
        
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

        mesh_uploaded_to_gpu_ = true;

        spdlog::info("Globe mesh uploaded to GPU: {} vertices, {} indices",
                    vertices.size() / 8, globe_indices_.size());

        return true;
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
        // Convert camera position to World coordinate type
        using namespace coordinates;
        World camera_world(camera_position);

        // Use centralized CoordinateMapper for visibility calculation
        GeographicBounds geo_bounds = CoordinateMapper::CalculateVisibleGeographicBounds(
            camera_world, view_matrix, projection_matrix, 1.0f);

        // Convert GeographicBounds to legacy BoundingBox2D format
        // BoundingBox2D uses (longitude, latitude) in x, y components
        return BoundingBox2D(
            glm::dvec2(geo_bounds.min.longitude, geo_bounds.min.latitude),
            glm::dvec2(geo_bounds.max.longitude, geo_bounds.max.latitude)
        );
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
    
    float CalculateTileLOD(const TileCoordinates& tile, float /*camera_distance*/) const {
        return static_cast<float>(tile.zoom);
    }

    float CalculateLoadPriority(const TileCoordinates& tile, const glm::vec3& /*camera_position*/) const {
        // For now, use zoom as priority (higher zoom = higher priority)
        return static_cast<float>(30 - tile.zoom); // Invert so higher zoom = lower number = higher priority
    }
    
    
};
// Note: TriggerTileLoading() removed - tile loading now handled by TileTextureCoordinator

// Factory function
std::unique_ptr<TileRenderer> TileRenderer::Create(const TileRenderConfig& config) {
    return std::make_unique<TileRendererImpl>(config);
}

} // namespace earth_map
