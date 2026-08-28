/**
 * @file tile_renderer.cpp
 * @brief Tile rendering system implementation
 */

#include <earth_map/renderer/tile_renderer.h>
#include <earth_map/renderer/geographic_quadtree.h>
#include <earth_map/imagery/tile_matrix_set.h>
#include <earth_map/renderer/globe_mesh.h>
#include <earth_map/renderer/shader_loader.h>
#include <earth_map/math/projection.h>
#include <earth_map/math/tile_mathematics.h>
#include <earth_map/renderer/texture_atlas/tile_texture_coordinator.h>
#include <earth_map/coordinates/coordinate_mapper.h>
#include <earth_map/constants.h>
#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <cstddef>
#include <optional>
#include <string>

#include "frame_zone_timing_collector.h"

namespace earth_map {

namespace {

constexpr int kMaxFallbackLevels = 5;
// A globe is lit by a distant sun, not a nearby point lamp. Keeping this
// vector normalized lets the fragment shader use a single dot product.
constexpr glm::vec3 kDefaultSunDirection{0.577350269f, 0.577350269f,
                                          0.577350269f};

constexpr int kMinZoom = 0;
constexpr int kMaxZoom = 21;
constexpr std::uint32_t kGeographicPatchGridSubdivisions = 16;

constexpr std::array<TileFragmentShadingProbe, 4> kTileFragmentShadingProbes = {
    TileFragmentShadingProbe::FullImagery,
    TileFragmentShadingProbe::FlatFill,
    TileFragmentShadingProbe::PatchLocalCoordinates,
    TileFragmentShadingProbe::UnlitImagery,
};

constexpr std::size_t TileFragmentProbeIndex(TileFragmentShadingProbe probe) {
    switch (probe) {
    case TileFragmentShadingProbe::FullImagery: return 0;
    case TileFragmentShadingProbe::FlatFill: return 1;
    case TileFragmentShadingProbe::PatchLocalCoordinates: return 2;
    case TileFragmentShadingProbe::UnlitImagery: return 3;
    }
    return 0;
}

const char* TileFragmentProbeName(TileFragmentShadingProbe probe) {
    switch (probe) {
    case TileFragmentShadingProbe::FullImagery: return "full";
    case TileFragmentShadingProbe::FlatFill: return "flat-fill";
    case TileFragmentShadingProbe::PatchLocalCoordinates: return "patch-local-coordinates";
    case TileFragmentShadingProbe::UnlitImagery: return "unlit-imagery";
    }
    return "full";
}

// Derived so that minimum camera altitude (100 m) maps to kMaxZoom.
// K = (MIN_ALTITUDE_METERS / EARTH_MEAN_RADIUS) × 2^kMaxZoom ≈ 32.9
// Every doubling of altitude decreases zoom by 1, matching the tile pyramid.
constexpr float kZoomAltitudeScale =
    (constants::camera_constraints::MIN_ALTITUDE_METERS
     / static_cast<float>(constants::geodetic::EARTH_MEAN_RADIUS))
    * static_cast<float>(1u << kMaxZoom);

/**
 * Returns the ancestors sampled by the fragment shader when an exact imagery
 * page is not resident.  The renderer still receives legacy TileCoordinates
 * at the icosphere boundary; canonical ImageTileKey resolution happens in the
 * coordinator before a request reaches cache, upload, or GPU residency.
 */
std::vector<TileCoordinates> BuildAncestorFallbackRequests(
    const std::vector<TileCoordinates>& leaf_tiles)
{
    std::vector<TileCoordinates> ancestors;
    std::unordered_set<TileCoordinates, TileCoordinatesHash> seen;

    for (const TileCoordinates& leaf : leaf_tiles) {
        TileCoordinates ancestor = leaf;
        for (int level = 1;
             level < kMaxFallbackLevels && ancestor.zoom > kMinZoom;
             ++level) {
            ancestor = ancestor.GetParent();
            if (seen.insert(ancestor).second) {
                ancestors.push_back(ancestor);
            }
        }
    }

    return ancestors;
}

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

/** Vertex submitted by the CPU-selected geographic patch path. */
struct GeographicPatchVertex {
    glm::vec3 position;
    glm::vec2 local_uv;
};

/** One direct texture-array draw of a selected geographic leaf patch. */
struct GeographicPatchDraw {
    renderer::ResolvedImageryPatch imagery;
    std::size_t vertex_offset = 0;
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

        zone_collector_.BeginFrame();

        frame_counter_++;
        stats_.rendered_tiles = 0;

        // Process GL uploads from worker threads (must be on GL thread)
        if (texture_coordinator_) {
            // Several mobile drivers synchronously stall around an elapsed
            // timer query that brackets texture uploads. Keep this as a CPU
            // zone so scenario timing measures the upload work itself.
            EARTH_MAP_CPU_ZONE_SCOPE(zone_collector_, upload_zone, "tile.upload");
            const TileTextureCoordinator::UploadProcessStats upload_stats =
                texture_coordinator_->ProcessUploads();
            stats_.upload_queue_depth_before = upload_stats.queue_depth_before;
            stats_.upload_queue_depth_after = upload_stats.queue_depth_after;
            stats_.upload_commands_processed = upload_stats.commands_processed;
            stats_.upload_commands_installed = upload_stats.commands_installed;
            stats_.tile_pool_upload_attempts = upload_stats.tile_pool_upload_attempts;
            stats_.tile_pool_upload_attempt_bytes = upload_stats.tile_pool_upload_attempt_bytes;
            stats_.upload_max_queue_wait_ms = upload_stats.max_queue_wait_ms;
            stats_.upload_total_command_cpu_ms = upload_stats.total_command_cpu_ms;
            stats_.upload_max_command_cpu_ms = upload_stats.max_command_cpu_ms;
            stats_.tile_pool_upload_total_cpu_ms =
                upload_stats.total_tile_pool_upload_cpu_ms;
            stats_.tile_pool_upload_max_cpu_ms = upload_stats.max_tile_pool_upload_cpu_ms;
            stats_.upload_eviction_total_cpu_ms = upload_stats.total_eviction_cpu_ms;
            stats_.upload_eviction_max_cpu_ms = upload_stats.max_eviction_cpu_ms;
            stats_.upload_residency_state_total_cpu_ms =
                upload_stats.total_residency_state_cpu_ms;
            stats_.upload_residency_state_max_cpu_ms =
                upload_stats.max_residency_state_cpu_ms;
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

        last_zone_timings_ = zone_collector_.EndFrame();
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
                        const glm::vec3& /*camera_position*/) override {
        if (!initialized_) {
            return;
        }

        // Clear previous visible tiles
        visible_tiles_.clear();

        glm::vec3 canonical_camera_position;
        float camera_distance = 0.0f;
        int zoom_level = kMinZoom;
        std::vector<TileCoordinates> visible_tile_coords;

        {
            // This zone covers source-matrix quadtree traversal on the
            // production path. Explicit diagnostic probes retain the old
            // TileMathematics candidate path for like-for-like attribution.
            EARTH_MAP_ZONE_SCOPE(zone_collector_, select_zone, "tile.cull.select");

            // The submitted view matrix defines the camera for both CPU
            // selection and the shader. Do not accept a second camera transform.
            canonical_camera_position = glm::vec3(glm::inverse(view_matrix)[3]);
            camera_distance = glm::length(canonical_camera_position);

            // Estimate optimal zoom level based on distance.
            zoom_level = CalculateOptimalZoom(camera_distance);

            // Every imagery path starts from the provider's declared source
            // matrix and refines the same geographic quadtree. Diagnostic
            // probes specialise only the direct patch fragment shader; they
            // must not switch back to a different renderer.
            if (texture_coordinator_) {
                const auto root_key = texture_coordinator_->GetDefaultImageryRootKey();
                const auto matrix_set = root_key.has_value()
                    ? texture_coordinator_->GetImageryTileMatrixSet(*root_key)
                    : std::nullopt;
                if (matrix_set.has_value()) {
                    renderer::GeographicPatchBounds visible_region{
                        -constants::math::PI,
                        matrix_set->minimum_latitude_radians,
                        constants::math::PI,
                        matrix_set->maximum_latitude_radians,
                    };
                    if (zoom_level > 4) {
                        const BoundingBox2D visible_bounds = CalculateVisibleGeographicBounds(
                            canonical_camera_position, view_matrix, projection_matrix);
                        visible_region = {
                            constants::conversion::DegreesToRadians(visible_bounds.min.x),
                            constants::conversion::DegreesToRadians(visible_bounds.min.y),
                            constants::conversion::DegreesToRadians(visible_bounds.max.x),
                            constants::conversion::DegreesToRadians(visible_bounds.max.y),
                        };
                    }

                    const renderer::GeographicQuadtreeSelectionConfig selection_config{
                        {visible_region},
                        std::clamp(static_cast<std::uint32_t>(zoom_level),
                                   matrix_set->minimum_level,
                                   matrix_set->maximum_level),
                        static_cast<std::size_t>(config_.max_visible_tiles),
                    };
                    const std::vector<imagery::ImageTileKey> selected_keys =
                        renderer::SelectVisibleGeographicQuadtreeLeaves(
                            *matrix_set,
                            root_key->imagery_source_id,
                            selection_config);
                    visible_tile_coords.reserve(selected_keys.size());
                    for (const imagery::ImageTileKey& key : selected_keys) {
                        if (key.address.column > static_cast<std::uint32_t>(
                                                    std::numeric_limits<std::int32_t>::max()) ||
                            key.address.row > static_cast<std::uint32_t>(
                                                  std::numeric_limits<std::int32_t>::max()) ||
                            key.address.level > static_cast<std::uint32_t>(
                                                    std::numeric_limits<std::int32_t>::max())) {
                            continue;
                        }
                        visible_tile_coords.emplace_back(
                            static_cast<std::int32_t>(key.address.column),
                            static_cast<std::int32_t>(key.address.row),
                            static_cast<std::int32_t>(key.address.level));
                    }
                }
            }

            if (visible_tile_coords.empty()) {
                // This fallback is only for startup before a provider source
                // declaration is available. Use int64_t because with
                // zoom_level 20, n*n overflows int32_t.
                const int64_t n = 1LL << zoom_level;
                if (n * n <= 256) {
                    // At low zoom (≤4), request all tiles — cheap and
                    // guarantees full coverage when ray-cast bounds are sparse.
                    visible_tile_coords.reserve(static_cast<std::size_t>(n * n));
                    for (int32_t x = 0; x < n; ++x) {
                        for (int32_t y = 0; y < n; ++y) {
                            visible_tile_coords.emplace_back(x, y, zoom_level);
                        }
                    }
                } else {
                    // At higher zoom, use visibility bounds from ray-cast
                    // geographic projection.
                    const BoundingBox2D visible_bounds = CalculateVisibleGeographicBounds(
                        canonical_camera_position, view_matrix, projection_matrix);
                    const std::vector<TileCoordinates> candidate_tiles =
                        TileMathematics::GetTilesInBounds(visible_bounds, zoom_level);

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
            }
        }

        // Request all ancestor pages that the shader can fall back to before
        // requesting the exact pages.  Lower worker-pool priorities run first;
        // this makes a direct high-zoom jump converge through real imagery,
        // rather than leaving an avoidable gray interval while exact children
        // download.  Both calls are idempotent.
        {
            EARTH_MAP_CPU_ZONE_SCOPE(zone_collector_, request_zone, "tile.cull.requests");
            if (texture_coordinator_ && !visible_tile_coords.empty()) {
                const std::vector<TileCoordinates> ancestor_tiles =
                    BuildAncestorFallbackRequests(visible_tile_coords);
                texture_coordinator_->UpdateActiveRequests(
                    visible_tile_coords, ancestor_tiles);
                texture_coordinator_->RequestTiles(ancestor_tiles, 1);
                texture_coordinator_->RequestTiles(visible_tile_coords, 0);

                // Keep both exact and fallback pages selected for this frame at
                // the front of the physical-layer LRU. This is render-thread
                // ownership, not a worker/cache mutation.
                texture_coordinator_->TouchTiles(ancestor_tiles);
                texture_coordinator_->TouchTiles(visible_tile_coords);
            }
        }

        {
            EARTH_MAP_ZONE_SCOPE(zone_collector_, state_zone, "tile.cull.state");

            // Build visible tiles list with UV coords from coordinator.
            for (const TileCoordinates& tile_coords : visible_tile_coords) {
                TileRenderState tile_state;
                tile_state.coordinates = tile_coords;
                tile_state.geographic_bounds = TileMathematics::GetTileBounds(tile_coords);
                tile_state.lod_level = CalculateTileLOD(tile_coords, camera_distance);
                tile_state.last_used = static_cast<float>(frame_counter_);
                tile_state.load_priority = CalculateLoadPriority(
                    tile_coords, canonical_camera_position);
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

            // Track visible tiles for change detection.
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

            UpdateGeographicPatchDraws(visible_tile_coords);
        }

        spdlog::debug("Tile renderer update: {} visible tiles, zoom level {}",
                    visible_tiles_.size(), zoom_level);
    }

    void RenderTiles(const glm::mat4& view_matrix,
                     const glm::mat4& projection_matrix) override {
        if (!initialized_) {
            return;
        }

        EARTH_MAP_ZONE_SCOPE(zone_collector_, draw_zone, "tile.draw");

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

        GLboolean depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cull_face_enabled = glIsEnabled(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        RenderDirectGeographicImagery(view_matrix, projection_matrix, draw_zone);
        if (!depth_test_enabled) {
            glDisable(GL_DEPTH_TEST);
        }
        if (!cull_face_enabled) {
            glDisable(GL_CULL_FACE);
        }

    }

    TileRenderStats GetStats() const override {
        TileRenderStats result = stats_;
        if (texture_coordinator_) {
            result.occupied_pool_layers = texture_coordinator_->GetPoolOccupiedLayers();
            result.max_pool_layers = texture_coordinator_->GetPoolMaxLayers();
            result.tile_pool_bytes_used = texture_coordinator_->GetPoolBytesUsed();
            result.tile_pool_bytes_max = texture_coordinator_->GetPoolBytesMax();
            result.pending_tile_loads = texture_coordinator_->GetPendingLoadCount();
        }
        return result;
    }

    std::vector<FrameZoneTiming> GetZoneTimings() const override {
        return last_zone_timings_;
    }

    TileRenderConfig GetConfig() const override {
        return config_;
    }

    void SetConfig(const TileRenderConfig& config) override {
        config_ = config;
        spdlog::info("Tile renderer config updated: max_tiles={}, fragment_probe={}",
                     config_.max_visible_tiles,
                     TileFragmentProbeName(config_.fragment_shading_probe));
    }

    void ClearCache() override {
        visible_tiles_.clear();
        geographic_patch_draws_.clear();
        geographic_patch_keys_.clear();
        geographic_patch_vertex_offsets_.clear();
        geographic_patch_vertices_.clear();
        geographic_patch_geometry_cache_.clear();
        geographic_patch_vertices_dirty_ = true;
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
    static glm::vec3 NormalizedRenderPosition(
        const geodesy::GeodeticPosition& geodetic) {
        // The existing camera/view pipeline still uses a unit sphere. Keep
        // that compatibility adapter at this boundary only: geographic patch
        // construction itself is WGS84 radians/ECEF, and the later
        // camera-relative ECEF migration can replace this conversion without
        // changing imagery selection, residency, or UV math.
        const double cos_latitude = std::cos(geodetic.latitude_radians);
        return {
            static_cast<float>(cos_latitude * std::sin(geodetic.longitude_radians)),
            static_cast<float>(std::sin(geodetic.latitude_radians)),
            static_cast<float>(cos_latitude * std::cos(geodetic.longitude_radians)),
        };
    }

    const std::vector<GeographicPatchVertex>* GetOrCreateGeographicPatchGeometry(
        const renderer::GeographicQuadtreePatch& patch) {
        const auto found = geographic_patch_geometry_cache_.find(patch.imagery_key);
        if (found != geographic_patch_geometry_cache_.end()) {
            return &found->second;
        }
        if (!geographic_patch_grid_.has_value()) {
            return nullptr;
        }

        std::vector<GeographicPatchVertex> vertices;
        vertices.reserve(geographic_patch_grid_->local_coordinates.size());
        for (const glm::vec2& local_uv_float : geographic_patch_grid_->local_coordinates) {
            const glm::dvec2 local_uv(local_uv_float.x, local_uv_float.y);
            const auto geodetic = renderer::PatchLocalToGeodetic(patch, local_uv);
            if (!geodetic.has_value()) {
                return nullptr;
            }
            vertices.push_back({NormalizedRenderPosition(*geodetic), local_uv_float});
        }

        const auto [inserted, was_inserted] = geographic_patch_geometry_cache_.emplace(
            patch.imagery_key, std::move(vertices));
        return was_inserted ? &inserted->second : nullptr;
    }

    void UpdateGeographicPatchDraws(
        const std::vector<TileCoordinates>& visible_tile_coords) {
        geographic_patch_draws_.clear();
        if (!texture_coordinator_ || !geographic_patch_grid_.has_value()) {
            return;
        }

        struct PendingPatchDraw {
            renderer::ResolvedImageryPatch imagery;
        };
        std::vector<PendingPatchDraw> pending_draws;
        std::vector<imagery::ImageTileKey> patch_keys;
        pending_draws.reserve(visible_tile_coords.size());
        patch_keys.reserve(visible_tile_coords.size());

        for (const TileCoordinates& tile_coords : visible_tile_coords) {
            const auto imagery_key = texture_coordinator_->ResolveImageryTileKey(tile_coords);
            if (!imagery_key.has_value()) {
                continue;
            }
            const auto matrix_set =
                texture_coordinator_->GetImageryTileMatrixSet(*imagery_key);
            if (!matrix_set.has_value()) {
                continue;
            }
            const auto patch = renderer::MakeGeographicQuadtreePatch(*matrix_set, *imagery_key);
            if (!patch.has_value()) {
                continue;
            }

            const auto resolved = renderer::ResolveResidentImageryPatch(
                *patch,
                kMaxFallbackLevels - 1,
                [this](const imagery::ImageTileKey& key) {
                    return texture_coordinator_->GetResidentImageryLayer(key);
                });
            if (!resolved.has_value()) {
                continue;
            }

            if (!GetOrCreateGeographicPatchGeometry(*patch)) {
                spdlog::warn("Failed to construct geographic imagery patch {}/{}/{}/{}/{}",
                             imagery_key->imagery_source_id,
                             imagery_key->matrix_set_id,
                             imagery_key->address.level,
                             imagery_key->address.column,
                             imagery_key->address.row);
                continue;
            }

            patch_keys.push_back(imagery_key.value());
            pending_draws.push_back({*resolved});
        }

        if (patch_keys != geographic_patch_keys_) {
            geographic_patch_vertices_.clear();
            geographic_patch_vertex_offsets_.clear();
            geographic_patch_vertices_.reserve(
                patch_keys.size() * geographic_patch_grid_->local_coordinates.size());

            for (const imagery::ImageTileKey& key : patch_keys) {
                const auto geometry = geographic_patch_geometry_cache_.find(key);
                if (geometry == geographic_patch_geometry_cache_.end()) {
                    continue;
                }
                geographic_patch_vertex_offsets_.emplace(
                    key, geographic_patch_vertices_.size());
                geographic_patch_vertices_.insert(
                    geographic_patch_vertices_.end(),
                    geometry->second.begin(), geometry->second.end());
            }

            geographic_patch_keys_ = std::move(patch_keys);
            geographic_patch_vertices_dirty_ = true;
        }

        geographic_patch_draws_.reserve(pending_draws.size());
        for (PendingPatchDraw& pending : pending_draws) {
            const auto offset = geographic_patch_vertex_offsets_.find(
                pending.imagery.patch.imagery_key);
            if (offset == geographic_patch_vertex_offsets_.end()) {
                continue;
            }
            geographic_patch_draws_.push_back({std::move(pending.imagery), offset->second});
        }
    }

    TileRenderConfig config_;
    TileTextureCoordinator* texture_coordinator_ = nullptr;
    GlobeMesh* globe_mesh_ = nullptr;  // External globe mesh to render on
    bool initialized_ = false;
    bool mesh_uploaded_to_gpu_ = false;  // Track if mesh data is on GPU
    std::uint64_t frame_counter_ = 0;
    std::vector<TileRenderState> visible_tiles_;
    std::optional<renderer::GeographicPatchGrid> geographic_patch_grid_;
    std::vector<GeographicPatchDraw> geographic_patch_draws_;
    std::vector<imagery::ImageTileKey> geographic_patch_keys_;
    std::unordered_map<imagery::ImageTileKey,
                       std::vector<GeographicPatchVertex>,
                       imagery::ImageTileKeyHash> geographic_patch_geometry_cache_;
    std::unordered_map<imagery::ImageTileKey,
                       std::size_t,
                       imagery::ImageTileKeyHash> geographic_patch_vertex_offsets_;
    std::vector<GeographicPatchVertex> geographic_patch_vertices_;
    bool geographic_patch_vertices_dirty_ = false;
    TileRenderStats stats_;
    FrameZoneTimingCollector zone_collector_;
    std::vector<FrameZoneTiming> last_zone_timings_;

    std::vector<TileCoordinates> last_visible_tiles_;

    struct BaseGlobeUniformLocations {
        GLint view = -1;
        GLint projection = -1;
        GLint model = -1;
        GLint light_direction = -1;
        GLint light_color = -1;
    };

    struct GeographicPatchUniformLocations {
        GLint view = -1;
        GLint projection = -1;
        GLint tile_pool = -1;
        GLint texture_layer = -1;
        GLint uv_scale = -1;
        GLint uv_offset = -1;
        GLint light_direction = -1;
        GLint light_color = -1;
    };

    struct GeographicPatchProgramState {
        std::uint32_t program = 0;
        GeographicPatchUniformLocations uniform_locs;
    };

    // The imagery probes specialise the same direct geographic-patch shader.
    // They never change tile selection, residency, or texture binding.
    std::array<GeographicPatchProgramState, kTileFragmentShadingProbes.size()>
        geographic_patch_programs_;
    std::uint32_t base_globe_program_ = 0;
    BaseGlobeUniformLocations base_globe_uniform_locs_;
    std::uint32_t globe_vao_ = 0;
    std::uint32_t globe_vbo_ = 0;
    std::uint32_t globe_ebo_ = 0;
    std::vector<unsigned int> globe_indices_;
    std::uint32_t geographic_patch_vao_ = 0;
    std::uint32_t geographic_patch_vbo_ = 0;
    std::uint32_t geographic_patch_ebo_ = 0;

    // Coarse base-globe vertex shader.
    static constexpr const char* kTileVertexShader = EARTH_MAP_GLSL_PREAMBLE R"(
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

    // Cheap base globe used where no imagery patch is resident yet.
    static constexpr const char* kBaseGlobeFragmentShader = EARTH_MAP_GLSL_PREAMBLE R"(
in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 uLightDirection;
uniform vec3 uLightColor;

void main() {
    const vec3 baseColor = vec3(0.07, 0.13, 0.21);
    const float ambientStrength = 0.25;
    float diffuse = max(dot(normalize(Normal), uLightDirection), 0.0);
    FragColor = vec4((ambientStrength + diffuse) * uLightColor * baseColor, 1.0);
}
)";

    // Geographic patches receive already projected CPU geometry and a direct
    // physical texture-array layer. No fragment ray construction, Mercator
    // conversion, GPU lookup, or parent fallback occurs here.
    static constexpr const char* kGeographicPatchVertexShader = EARTH_MAP_GLSL_PREAMBLE R"(
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aLocalUv;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 WorldPosition;
out vec2 LocalUv;

void main() {
    WorldPosition = aPosition;
    LocalUv = aLocalUv;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
)";

    static constexpr const char* kGeographicPatchFragmentShaderBody = R"(
#ifndef EARTH_MAP_TILE_FRAGMENT_PROBE
#define EARTH_MAP_TILE_FRAGMENT_PROBE 0
#endif

in vec3 WorldPosition;
in vec2 LocalUv;

out vec4 FragColor;

uniform sampler2DArray uTilePool;
uniform float uTextureLayer;
uniform vec2 uUvScale;
uniform vec2 uUvOffset;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;

void main() {
#if EARTH_MAP_TILE_FRAGMENT_PROBE == 1
    FragColor = vec4(0.20, 0.45, 0.85, 1.0);
    return;
#elif EARTH_MAP_TILE_FRAGMENT_PROBE == 2
    FragColor = vec4(LocalUv, 0.0, 1.0);
    return;
#endif

    const float kHalfTexel = 0.5 / 256.0;
    vec2 uv = uUvOffset + LocalUv * uUvScale;
    uv = clamp(uv, kHalfTexel, 1.0 - kHalfTexel);
    vec4 texColor = texture(uTilePool, vec3(uv, uTextureLayer));

 #if EARTH_MAP_TILE_FRAGMENT_PROBE == 3
    FragColor = texColor;
 #else
    const float ambientStrength = 0.25;
    float diffuse = max(dot(normalize(WorldPosition), uLightDirection), 0.0);
    FragColor = vec4((ambientStrength + diffuse) * uLightColor * texColor.rgb,
                     texColor.a);
 #endif
}
)";

    static std::string BuildGeographicPatchFragmentShader(
        TileFragmentShadingProbe probe) {
        std::string source = EARTH_MAP_GLSL_PREAMBLE;
        source += "#define EARTH_MAP_TILE_FRAGMENT_PROBE ";
        source += std::to_string(TileFragmentProbeIndex(probe));
        source += "\n";
        source += kGeographicPatchFragmentShaderBody;
        return source;
    }

    void CacheBaseGlobeUniformLocations() {
        base_globe_uniform_locs_.view =
            glGetUniformLocation(base_globe_program_, "uView");
        base_globe_uniform_locs_.projection =
            glGetUniformLocation(base_globe_program_, "uProjection");
        base_globe_uniform_locs_.model =
            glGetUniformLocation(base_globe_program_, "uModel");
        base_globe_uniform_locs_.light_direction =
            glGetUniformLocation(base_globe_program_, "uLightDirection");
        base_globe_uniform_locs_.light_color =
            glGetUniformLocation(base_globe_program_, "uLightColor");
    }

    void CacheGeographicPatchUniformLocations(GeographicPatchProgramState& state) {
        const std::uint32_t program = state.program;
        GeographicPatchUniformLocations& uniform_locs = state.uniform_locs;
        uniform_locs.view = glGetUniformLocation(program, "uView");
        uniform_locs.projection = glGetUniformLocation(program, "uProjection");
        uniform_locs.tile_pool = glGetUniformLocation(program, "uTilePool");
        uniform_locs.texture_layer = glGetUniformLocation(program, "uTextureLayer");
        uniform_locs.uv_scale = glGetUniformLocation(program, "uUvScale");
        uniform_locs.uv_offset = glGetUniformLocation(program, "uUvOffset");
        uniform_locs.light_direction = glGetUniformLocation(program, "uLightDirection");
        uniform_locs.light_color = glGetUniformLocation(program, "uLightColor");
    }

    void CleanupShaderPrograms() {
        for (GeographicPatchProgramState& shader_state : geographic_patch_programs_) {
            if (shader_state.program != 0) {
                glDeleteProgram(shader_state.program);
                shader_state.program = 0;
            }
        }

        if (base_globe_program_ != 0) {
            glDeleteProgram(base_globe_program_);
            base_globe_program_ = 0;
        }
    }

    bool InitializeOpenGLState() {
        for (const TileFragmentShadingProbe probe : kTileFragmentShadingProbes) {
            GeographicPatchProgramState& shader_state =
                geographic_patch_programs_[TileFragmentProbeIndex(probe)];
            const std::string fragment_shader = BuildGeographicPatchFragmentShader(probe);
            const std::string program_name =
                std::string("geographic_imagery_patch.") + TileFragmentProbeName(probe);
            shader_state.program = ShaderLoader::CreateProgram(
                kGeographicPatchVertexShader, fragment_shader.c_str(), program_name);
            if (shader_state.program == 0) {
                spdlog::error("Failed to create {} shader program", program_name);
                CleanupShaderPrograms();
                return false;
            }
            CacheGeographicPatchUniformLocations(shader_state);
        }

        base_globe_program_ = ShaderLoader::CreateProgram(
            kTileVertexShader, kBaseGlobeFragmentShader, "geographic_base_globe");
        if (base_globe_program_ == 0) {
            spdlog::error("Failed to create geographic base globe shader program");
            CleanupShaderPrograms();
            return false;
        }
        CacheBaseGlobeUniformLocations();

        geographic_patch_grid_ = renderer::MakeGeographicPatchGrid(
            kGeographicPatchGridSubdivisions);
        if (!geographic_patch_grid_.has_value()) {
            spdlog::error("Failed to create geographic imagery patch grid");
            CleanupShaderPrograms();
            return false;
        }
        InitializeGeographicPatchGeometry();

        spdlog::info("Tile renderer OpenGL state initialized with a direct geographic "
                     "imagery patch path and {} fragment probes "
                     "(mesh will be uploaded when provided)",
                     geographic_patch_programs_.size());
        return true;
    }

    void InitializeGeographicPatchGeometry() {
        glGenVertexArrays(1, &geographic_patch_vao_);
        glGenBuffers(1, &geographic_patch_vbo_);
        glGenBuffers(1, &geographic_patch_ebo_);

        glBindVertexArray(geographic_patch_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, geographic_patch_vbo_);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geographic_patch_ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     geographic_patch_grid_->indices.size() * sizeof(std::uint32_t),
                     geographic_patch_grid_->indices.data(),
                     GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              sizeof(GeographicPatchVertex),
                              reinterpret_cast<const void*>(offsetof(
                                  GeographicPatchVertex, position)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                              sizeof(GeographicPatchVertex),
                              reinterpret_cast<const void*>(offsetof(
                                  GeographicPatchVertex, local_uv)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    void UploadGeographicPatchVertices() {
        if (!geographic_patch_vertices_dirty_) {
            return;
        }

        glBindBuffer(GL_ARRAY_BUFFER, geographic_patch_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     geographic_patch_vertices_.size() * sizeof(GeographicPatchVertex),
                     geographic_patch_vertices_.empty()
                         ? nullptr
                         : geographic_patch_vertices_.data(),
                     GL_DYNAMIC_DRAW);
        geographic_patch_vertices_dirty_ = false;
    }

    void RenderBaseGlobe(const glm::mat4& view_matrix,
                         const glm::mat4& projection_matrix) {
        glUseProgram(base_globe_program_);
        glUniformMatrix4fv(base_globe_uniform_locs_.view, 1, GL_FALSE,
                           glm::value_ptr(view_matrix));
        glUniformMatrix4fv(base_globe_uniform_locs_.projection, 1, GL_FALSE,
                           glm::value_ptr(projection_matrix));
        glUniformMatrix4fv(base_globe_uniform_locs_.model, 1, GL_FALSE,
                           glm::value_ptr(glm::mat4(1.0f)));
        glUniform3f(base_globe_uniform_locs_.light_direction,
                    kDefaultSunDirection.x, kDefaultSunDirection.y,
                    kDefaultSunDirection.z);
        glUniform3f(base_globe_uniform_locs_.light_color, 1.0f, 1.0f, 1.0f);

        glBindVertexArray(globe_vao_);
        glDrawElements(GL_TRIANGLES, globe_indices_.size(), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    void RenderGeographicPatches(const glm::mat4& view_matrix,
                                 const glm::mat4& projection_matrix,
                                 FrameZoneScope& draw_zone) {
        if (geographic_patch_draws_.empty() || !texture_coordinator_) {
            return;
        }

        UploadGeographicPatchVertices();
        const GeographicPatchProgramState& program_state =
            geographic_patch_programs_[TileFragmentProbeIndex(config_.fragment_shading_probe)];
        const GeographicPatchUniformLocations& uniform_locs = program_state.uniform_locs;
        glUseProgram(program_state.program);
        glUniformMatrix4fv(uniform_locs.view, 1, GL_FALSE,
                           glm::value_ptr(view_matrix));
        glUniformMatrix4fv(uniform_locs.projection, 1, GL_FALSE,
                           glm::value_ptr(projection_matrix));
        glUniform3f(uniform_locs.light_direction,
                    kDefaultSunDirection.x, kDefaultSunDirection.y,
                    kDefaultSunDirection.z);
        glUniform3f(uniform_locs.light_color,
                    1.0f, 1.0f, 1.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY,
                      texture_coordinator_->GetTilePoolTextureID());
        glUniform1i(uniform_locs.tile_pool, 0);

        glBindVertexArray(geographic_patch_vao_);
        // glVertexAttribPointer captures the currently bound array buffer in
        // this VAO. RenderBaseGlobe may have left a different VBO bound on a
        // previous frame, so bind the patch VBO explicitly before rebasing
        // the attributes for each cached patch range.
        glBindBuffer(GL_ARRAY_BUFFER, geographic_patch_vbo_);
        for (const GeographicPatchDraw& draw : geographic_patch_draws_) {
            glUniform1f(uniform_locs.texture_layer,
                        static_cast<float>(draw.imagery.texture_layer));
            glUniform2f(uniform_locs.uv_scale,
                        static_cast<float>(draw.imagery.uv_scale.x),
                        static_cast<float>(draw.imagery.uv_scale.y));
            glUniform2f(uniform_locs.uv_offset,
                        static_cast<float>(draw.imagery.uv_offset.x),
                        static_cast<float>(draw.imagery.uv_offset.y));

            const std::size_t byte_offset =
                draw.vertex_offset * sizeof(GeographicPatchVertex);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                  sizeof(GeographicPatchVertex),
                                  reinterpret_cast<const void*>(
                                      byte_offset + offsetof(GeographicPatchVertex, position)));
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                                  sizeof(GeographicPatchVertex),
                                  reinterpret_cast<const void*>(
                                      byte_offset + offsetof(GeographicPatchVertex, local_uv)));
            glDrawElements(GL_TRIANGLES,
                           geographic_patch_grid_->indices.size(),
                           GL_UNSIGNED_INT,
                           nullptr);
            draw_zone.AddDrawCall(geographic_patch_grid_->indices.size() / 3U);
        }
        glBindVertexArray(0);
    }

    void RenderDirectGeographicImagery(const glm::mat4& view_matrix,
                                        const glm::mat4& projection_matrix,
                                        FrameZoneScope& draw_zone) {
        RenderBaseGlobe(view_matrix, projection_matrix);

        // Patches are an imagery overlay over the legacy icosphere. The two
        // independently tessellated surfaces do not share vertices, so depth
        // testing would create false holes where the coarse patch chord falls
        // microscopically inside the finer base mesh. Front/back culling still
        // rejects the far hemisphere. Terrain will replace this temporary
        // overlay relationship with one shared patch surface and depth.
        const GLboolean depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        RenderGeographicPatches(view_matrix, projection_matrix, draw_zone);
        if (depth_test_enabled) {
            glEnable(GL_DEPTH_TEST);
        }

        stats_.rendered_tiles = geographic_patch_draws_.size();
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
        if (geographic_patch_vao_) {
            glDeleteVertexArrays(1, &geographic_patch_vao_);
            geographic_patch_vao_ = 0;
        }
        if (geographic_patch_vbo_) {
            glDeleteBuffers(1, &geographic_patch_vbo_);
            geographic_patch_vbo_ = 0;
        }
        if (geographic_patch_ebo_) {
            glDeleteBuffers(1, &geographic_patch_ebo_);
            geographic_patch_ebo_ = 0;
        }
        CleanupShaderPrograms();
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
