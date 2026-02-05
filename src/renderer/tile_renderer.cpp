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
#include <earth_map/renderer/tile_pool/indirection_texture_manager.h>
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

constexpr int kDefaultTileSize = 256;
constexpr int kDefaultZoomLevel = 2;
constexpr int kMaxFallbackLevels = 5;
constexpr glm::vec3 kDefaultLightPosition{2.0f, 2.0f, 2.0f};

constexpr int kMinZoom = 0;
constexpr int kMaxZoom = 21;

// Derived so that minimum camera altitude (100 m) maps to kMaxZoom.
// K = (MIN_ALTITUDE_METERS / EARTH_MEAN_RADIUS) × 2^kMaxZoom ≈ 32.9
// Every doubling of altitude decreases zoom by 1, matching the tile pyramid.
constexpr float kZoomAltitudeScale =
    (constants::camera_constraints::MIN_ALTITUDE_METERS
     / static_cast<float>(constants::geodetic::EARTH_MEAN_RADIUS))
    * static_cast<float>(1u << kMaxZoom);

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

        // Process GL uploads from worker threads (must be on GL thread)
        if (texture_coordinator_) {
            texture_coordinator_->ProcessUploads(100);  // Upload up to 5 tiles per frame for 60 FPS (changed)
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
                        const Frustum& /*frustum*/) override {
        if (!initialized_ || !tile_manager_) {
            return;
        }
        
        // Clear previous visible tiles
        visible_tiles_.clear();
        
        // Calculate camera distance from globe center
        const float camera_distance = glm::length(camera_position);
        
        // Estimate optimal zoom level based on distance
        const int zoom_level = CalculateOptimalZoom(camera_distance);
        
        // Collect visible tile coordinates
        // Using int64_t to avoid overflow, because with zoom_level 20, n equals 1048576 and n * n gives 0 with int32_t
        const int64_t n = 1 << zoom_level;
        std::vector<TileCoordinates> visible_tile_coords;

        if (n * n <= 256) {
            // At low zoom (≤4), request all tiles — cheap and guarantees full coverage.
            // This avoids issues with CalculateVisibleGeographicBounds() ray-sphere
            // intersection failures that cause missing tiles.
            visible_tile_coords.reserve(n * n);
            for (int32_t x = 0; x < n; ++x) {
                for (int32_t y = 0; y < n; ++y) {
                    visible_tile_coords.emplace_back(x, y, zoom_level);
                }
            }
        } else {
            // At higher zoom, use visibility bounds from ray-cast geographic projection
            const BoundingBox2D visible_bounds = CalculateVisibleGeographicBounds(
                camera_position, view_matrix, projection_matrix);
            const std::vector<TileCoordinates> candidate_tiles =
                tile_manager_->GetTilesInBounds(visible_bounds, zoom_level);

            spdlog::info("Tag. Candidate tiles: {} with n = {} and zoom level = {}", candidate_tiles.size(), n, zoom_level);

            const std::size_t max_tiles_for_frame =
                static_cast<std::size_t>(config_.max_visible_tiles);

            if (candidate_tiles.size() <= max_tiles_for_frame) {
                visible_tile_coords = candidate_tiles;
            } else {
                visible_tile_coords.assign(
                    candidate_tiles.begin(),
                    candidate_tiles.begin() + max_tiles_for_frame);
            }
        }

        // Update indirection window center for CURRENT zoom level only (13+).
        // Fallback levels keep their existing windows - their tiles were loaded when
        // the camera was at those zoom levels. Updating fallback windows with current
        // camera position would clear their tiles (if shift > 512), causing GRAY areas.
        if (texture_coordinator_ && zoom_level > IndirectionTextureManager::kMaxFullIndirectionZoom) {
            const coordinates::Geographic cam_geo =
                coordinates::CoordinateMapper::CartesianToGeographic(camera_position);
            const TileCoordinates center_tile =
                coordinates::CoordinateMapper::GeographicToSphericalTile(cam_geo, zoom_level);
            texture_coordinator_->UpdateIndirectionWindowCenter(
                zoom_level, center_tile.x, center_tile.y);
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
        glUniformMatrix4fv(uniform_locs_.view, 1, GL_FALSE, glm::value_ptr(view_matrix));
        glUniformMatrix4fv(uniform_locs_.projection, 1, GL_FALSE, glm::value_ptr(projection_matrix));
        glUniformMatrix4fv(uniform_locs_.model, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));

        // Set lighting uniforms
        glUniform3f(uniform_locs_.light_pos, kDefaultLightPosition.x, kDefaultLightPosition.y, kDefaultLightPosition.z);
        glUniform3f(uniform_locs_.light_color, 1.0f, 1.0f, 1.0f);

        // Get current zoom level from visible tiles (or use default)
        int current_zoom = kDefaultZoomLevel;
        if (!visible_tiles_.empty()) {
            current_zoom = visible_tiles_[0].coordinates.zoom;
        }

        // Set zoom and fallback level uniforms
        glUniform1i(uniform_locs_.zoom_level, current_zoom);

        const int num_fallback = std::min(kMaxFallbackLevels, current_zoom + 1);
        glUniform1i(uniform_locs_.num_fallback_levels, num_fallback);

        // Bind tile pool texture array to unit 0
        glActiveTexture(GL_TEXTURE0);
        std::uint32_t pool_texture_id = 0;
        if (texture_coordinator_) {
            pool_texture_id = texture_coordinator_->GetTilePoolTextureID();
        }
        glBindTexture(GL_TEXTURE_2D_ARRAY, pool_texture_id);

        glUniform1i(uniform_locs_.tile_pool, 0);

        // Bind indirection textures for zoom Z through Z-(N-1) to units 1..N.
        // Always bind ALL 5 units to valid GL_TEXTURE_2D targets.
        // Unused levels get the dummy 1x1 texture (kInvalidLayer).
        // This prevents undefined behavior from sampler/target type mismatch
        // (usampler2D pointing at GL_TEXTURE_2D_ARRAY on unit 0).
        for (int level = 0; level < kMaxFallbackLevels; ++level) {
            const int zoom = current_zoom - level;
            const GLint tex_unit = 1 + level;

            glActiveTexture(GL_TEXTURE0 + tex_unit);

            std::uint32_t indirection_id = 0;
            glm::ivec2 offset(0, 0);
            if (texture_coordinator_ && zoom >= 0) {
                indirection_id = texture_coordinator_->GetIndirectionTextureID(zoom);
                offset = texture_coordinator_->GetIndirectionOffset(zoom);
            } else if (texture_coordinator_) {
                indirection_id = texture_coordinator_->GetIndirectionTextureID(-1);
            }

            glBindTexture(GL_TEXTURE_2D, indirection_id);
            glUniform1i(uniform_locs_.indirection[level], tex_unit);
            glUniform2i(uniform_locs_.indirection_offset[level], offset.x, offset.y);
        }

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
        stats_.texture_binds = 1 + kMaxFallbackLevels;  // tile pool + indirection textures
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
        // Cache cleared
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
    
    std::vector<TileCoordinates> last_visible_tiles_;
    
    // OpenGL objects
    std::uint32_t tile_shader_program_ = 0;

    // Cached uniform locations (populated after shader compilation)
    struct UniformLocations {
        GLint view = -1;
        GLint projection = -1;
        GLint model = -1;
        GLint light_pos = -1;
        GLint light_color = -1;
        GLint zoom_level = -1;
        GLint num_fallback_levels = -1;
        GLint tile_pool = -1;
        GLint indirection[5] = {-1, -1, -1, -1, -1};
        GLint indirection_offset[5] = {-1, -1, -1, -1, -1};
    } uniform_locs_;
    std::uint32_t globe_vao_ = 0;
    std::uint32_t globe_vbo_ = 0;
    std::uint32_t globe_ebo_ = 0;
    std::vector<unsigned int> globe_indices_;
    
    // Tile atlas vertex shader source
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

    // Tile pool + indirection fragment shader
    static constexpr const char* kTileFragmentShader = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2DArray uTilePool;
uniform usampler2D uIndirection0;
uniform usampler2D uIndirection1;
uniform usampler2D uIndirection2;
uniform usampler2D uIndirection3;
uniform usampler2D uIndirection4;
uniform int uZoomLevel;
uniform int uNumFallbackLevels;
uniform ivec2 uIndirectionOffset0;
uniform ivec2 uIndirectionOffset1;
uniform ivec2 uIndirectionOffset2;
uniform ivec2 uIndirectionOffset3;
uniform ivec2 uIndirectionOffset4;
uniform vec3 uLightPos;
uniform vec3 uLightColor;

vec2 worldToGeo(vec3 pos) {
    const float PI = 3.14159265358979323846;
    vec3 n = normalize(pos);
    return vec2(atan(n.x, n.z) * 180.0 / PI,
                asin(n.y) * 180.0 / PI);
}

void geoToTileAndFrac(vec2 geo, int zoom, out ivec2 tile, out vec2 frac) {
    const float PI = 3.14159265358979323846;

    // Compute normalized coordinates (0-1 range) first to preserve precision.
    // Scaling by n happens last to avoid large intermediate float values
    // that lose precision at high zoom levels (n = 524288 at zoom 19).
    float norm_x = (geo.x + 180.0) / 360.0;
    float lat_rad = clamp(geo.y, -85.0511, 85.0511) * PI / 180.0;
    float norm_y = (1.0 - log(tan(PI / 4.0 + lat_rad / 2.0)) / PI) / 2.0;

    // Scale by tile count at this zoom level
    int n = 1 << zoom;
    float fx = norm_x * float(n);
    float fy = norm_y * float(n);

    tile = ivec2(clamp(int(floor(fx)), 0, n - 1),
                 clamp(int(floor(fy)), 0, n - 1));

    // Use subtraction instead of fract() to avoid precision loss on large values
    frac = vec2(fx - floor(fx), fy - floor(fy));
}

uint safeFetch(usampler2D tex, ivec2 coord) {
    ivec2 sz = textureSize(tex, 0);
    if (coord.x < 0 || coord.y < 0 || coord.x >= sz.x || coord.y >= sz.y)
        return 0xFFFFu;
    return texelFetch(tex, coord, 0).r;
}

uint lookupLayer(int level, ivec2 tile) {
    if      (level == 0) return safeFetch(uIndirection0, tile - uIndirectionOffset0);
    else if (level == 1) return safeFetch(uIndirection1, tile - uIndirectionOffset1);
    else if (level == 2) return safeFetch(uIndirection2, tile - uIndirectionOffset2);
    else if (level == 3) return safeFetch(uIndirection3, tile - uIndirectionOffset3);
    else                 return safeFetch(uIndirection4, tile - uIndirectionOffset4);
}

// Diagnostic visualization: shows which fallback level is being used
// Uncomment to enable (1 = diagnostic mode, 0 = normal rendering)
const int kDiagnosticMode = 1;

// Fallback level colors (RGB)
const vec3 kColorLevel0 = vec3(0.0, 1.0, 0.0);  // Green = exact zoom
const vec3 kColorLevel1 = vec3(1.0, 1.0, 0.0);  // Yellow = parent
const vec3 kColorLevel2 = vec3(1.0, 0.5, 0.0);  // Orange = grandparent  
const vec3 kColorLevel3 = vec3(1.0, 0.0, 0.0);  // Red = great-grandparent
const vec3 kColorLevel4 = vec3(1.0, 0.0, 1.0);  // Magenta = great-great-grandparent
const vec3 kColorMissing = vec3(0.5, 0.5, 0.5); // Gray = no tile found

void main() {
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * uLightColor;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;

    vec2 geo = worldToGeo(FragPos);

    for (int level = 0; level < uNumFallbackLevels; level++) {
        int zoom = uZoomLevel - level;
        if (zoom < 0) break;

        ivec2 tile;
        vec2 frac;
        geoToTileAndFrac(geo, zoom, tile, frac);

        uint layerIdx = lookupLayer(level, tile);

        if (layerIdx != 0xFFFFu) {
            // Clamp UV to prevent GL_LINEAR filtering from sampling outside tile boundaries.
            // Half-texel inset prevents bleeding from adjacent tiles in the texture array.
            const float kHalfTexel = 0.5 / 256.0;
            vec2 clamped_frac = clamp(frac, kHalfTexel, 1.0 - kHalfTexel);
            vec4 texColor = texture(uTilePool, vec3(clamped_frac, float(layerIdx)));
            
            // Apply diagnostic overlay if enabled
            vec3 finalColor = (ambient + diffuse) * texColor.rgb;
            if (kDiagnosticMode == 1) {
                vec3 diagColor;
                if (level == 0) diagColor = kColorLevel0;
                else if (level == 1) diagColor = kColorLevel1;
                else if (level == 2) diagColor = kColorLevel2;
                else if (level == 3) diagColor = kColorLevel3;
                else diagColor = kColorLevel4;
                
                // Blend tile color with diagnostic color (50/50 blend)
                finalColor = mix(finalColor, diagColor, 0.5);
            }
            
            FragColor = vec4(finalColor, texColor.a);
            return;
        }
    }

    vec3 baseColor = kDiagnosticMode == 1 ? kColorMissing : vec3(0.85, 0.82, 0.75);
    FragColor = vec4((ambient + diffuse) * baseColor, 1.0);
}
)";

    bool InitializeOpenGLState() {
        tile_shader_program_ = ShaderLoader::CreateProgram(
            kTileVertexShader, kTileFragmentShader, "tile_atlas");

        if (tile_shader_program_ == 0) {
            spdlog::error("Failed to create tile atlas shader program");
            return false;
        }

        // Cache uniform locations
        uniform_locs_.view = glGetUniformLocation(tile_shader_program_, "uView");
        uniform_locs_.projection = glGetUniformLocation(tile_shader_program_, "uProjection");
        uniform_locs_.model = glGetUniformLocation(tile_shader_program_, "uModel");
        uniform_locs_.light_pos = glGetUniformLocation(tile_shader_program_, "uLightPos");
        uniform_locs_.light_color = glGetUniformLocation(tile_shader_program_, "uLightColor");
        uniform_locs_.zoom_level = glGetUniformLocation(tile_shader_program_, "uZoomLevel");
        uniform_locs_.num_fallback_levels = glGetUniformLocation(tile_shader_program_, "uNumFallbackLevels");
        uniform_locs_.tile_pool = glGetUniformLocation(tile_shader_program_, "uTilePool");

        const char* indirection_names[] = {
            "uIndirection0", "uIndirection1", "uIndirection2",
            "uIndirection3", "uIndirection4"
        };
        const char* offset_names[] = {
            "uIndirectionOffset0", "uIndirectionOffset1", "uIndirectionOffset2",
            "uIndirectionOffset3", "uIndirectionOffset4"
        };
        for (int i = 0; i < kMaxFallbackLevels; ++i) {
            uniform_locs_.indirection[i] = glGetUniformLocation(tile_shader_program_, indirection_names[i]);
            uniform_locs_.indirection_offset[i] = glGetUniformLocation(tile_shader_program_, offset_names[i]);
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
        const float altitude = camera_distance - 1.0f;

        if (altitude <= 0.0f) {
            return kMaxZoom;
        }

        // Single logarithmic mapping: zoom = log2(K / altitude).
        // K (kZoomAltitudeScale) is calibrated so that min camera altitude
        // maps to kMaxZoom. Each doubling of altitude drops zoom by 1,
        // matching the tile pyramid where each level doubles tile count.
        const float zoom = std::log2(kZoomAltitudeScale / altitude);

        spdlog::info("Tag. Picked zoom: {} for camera distance: {}", zoom, camera_distance);
        return std::clamp(static_cast<int>(zoom), kMinZoom, kMaxZoom);
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
        // Original min/max
        glm::dvec2 min(geo_bounds.min.longitude, geo_bounds.min.latitude);
        glm::dvec2 max(geo_bounds.max.longitude, geo_bounds.max.latitude);

        // // Center of the bounds
        // glm::dvec2 center = (min + max) * 0.5;

        // // Half extents
        // glm::dvec2 half_extents = (max - min) * 0.5;

        // // Scale factor: keep 20% (reduce by 80%)
        // constexpr double scale = 0.2;

        // half_extents *= scale;

        // // New reduced bounds
        // glm::dvec2 reduced_min = center - half_extents;
        // glm::dvec2 reduced_max = center + half_extents;

        // return BoundingBox2D(reduced_min, reduced_max);
        return BoundingBox2D(min, max);
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
