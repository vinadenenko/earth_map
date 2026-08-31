/**
 * @file tile_renderer.cpp
 * @brief Tile rendering system implementation
 */

#include <earth_map/renderer/tile_renderer.h>
#include <earth_map/renderer/geographic_quadtree.h>
#include "ecef_render_frame.h"
#include <earth_map/imagery/tile_matrix_set.h>
#include <earth_map/renderer/shader_loader.h>
#include <earth_map/math/projection.h>
#include <earth_map/math/tile_mathematics.h>
#include <earth_map/renderer/texture_atlas/tile_texture_coordinator.h>
#include <earth_map/constants.h>
#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#else
#include <GL/glew.h>
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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

// Derived so that minimum camera altitude (MIN_ALTITUDE_METERS) maps to
// kMaxZoom. Every doubling of altitude decreases zoom by 1, matching the
// tile pyramid where each level doubles tile count. This is the real-metres
// port of main's original normalized-unit formula: the EARTH_MEAN_RADIUS
// normalization term cancels exactly once altitude is already in metres.
constexpr double kZoomAltitudeScale =
    static_cast<double>(constants::camera_constraints::MIN_ALTITUDE_METERS) *
    static_cast<double>(std::uint64_t{1} << kMaxZoom);

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

#ifdef __ANDROID__

using DrawElementsBaseVertexFn = void (*)(GLenum, GLsizei, GLenum, const void*, GLint);

bool ExtensionStringPresent(const char* name) {
    GLint extension_count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
    for (GLint i = 0; i < extension_count; ++i) {
        const auto* extension =
            reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (extension && std::strcmp(extension, name) == 0) {
            return true;
        }
    }
    return false;
}

// glDrawElementsBaseVertex is core desktop GL (3.2+) but not core in GLES
// 3.0 (this project's Android baseline) -- it was only folded into core in
// GLES 3.2. Before that it shipped as one of two equivalent, widely
// supported optional extensions; resolve whichever is present once, lazily,
// via eglGetProcAddress (same approach as GL_EXT_disjoint_timer_query in
// gpu_elapsed_time_query.cpp). Devices with neither fall back to per-patch
// vertex attribute rebinding in RenderGeographicPatches.
DrawElementsBaseVertexFn ResolveDrawElementsBaseVertex() {
    if (ExtensionStringPresent("GL_EXT_draw_elements_base_vertex")) {
        if (auto* proc = reinterpret_cast<DrawElementsBaseVertexFn>(
                eglGetProcAddress("glDrawElementsBaseVertexEXT"))) {
            return proc;
        }
    }
    if (ExtensionStringPresent("GL_OES_draw_elements_base_vertex")) {
        if (auto* proc = reinterpret_cast<DrawElementsBaseVertexFn>(
                eglGetProcAddress("glDrawElementsBaseVertexOES"))) {
            return proc;
        }
    }
    spdlog::info("glDrawElementsBaseVertex unavailable (neither GL_EXT_ nor "
                 "GL_OES_draw_elements_base_vertex present) -- geographic "
                 "patches will fall back to per-patch vertex attribute rebinding");
    return nullptr;
}

DrawElementsBaseVertexFn GetDrawElementsBaseVertex() {
    static const DrawElementsBaseVertexFn proc = ResolveDrawElementsBaseVertex();
    return proc;
}

#endif  // __ANDROID__

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
 * Vertex submitted by the CPU-selected geographic patch path. atlas_uv and
 * texture_layer are baked in per-vertex (from the patch's resolved imagery)
 * rather than bound as per-draw uniforms, so every patch's geometry is
 * self-describing and no per-draw state change is needed to select the
 * right texture-array layer/UV rect -- see RenderGeographicPatches.
 */
struct GeographicPatchVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 local_uv;
    glm::vec2 atlas_uv;
    float texture_layer = 0.0f;
};

/**
 * Frame-independent patch vertex: ellipsoid geometry in double-precision
 * ECEF, cached once per patch identity. GeographicPatchVertex above is the
 * cheap per-frame projection of this into the camera-relative float frame.
 */
struct GeographicPatchVertexEcef {
    glm::dvec3 ecef_position;
    glm::dvec3 ecef_normal;
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
                texture_coordinator_->ProcessUploads(
                    static_cast<int>(config_.max_tile_uploads_per_frame));
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

    void UpdateVisibleTiles(const glm::mat4& view_matrix,
                            const glm::mat4& projection_matrix,
                            const geodesy::EcefPosition& camera_position) override {
        if (!initialized_) {
            return;
        }

        // Clear previous visible tiles
        visible_tiles_.clear();

        const auto camera_geodetic = geodesy::Wgs84Ellipsoid::FromEcef(camera_position);
        if (!camera_geodetic.has_value()) {
            spdlog::error("Tile renderer: refusing an invalid ECEF camera position");
            return;
        }
        render_frame_ = renderer::EcefRenderFrame::FromCamera(*camera_geodetic);
        int zoom_level = kMinZoom;
        std::vector<TileCoordinates> visible_tile_coords;

        {
            // This zone covers source-matrix quadtree traversal on the
            // production path. Explicit diagnostic probes retain the old
            // TileMathematics candidate path for like-for-like attribution.
            EARTH_MAP_ZONE_SCOPE(zone_collector_, select_zone, "tile.cull.select");

            // Zoom follows camera altitude directly (always well-defined,
            // monotonic) rather than the screen-footprint estimate below --
            // that estimate is a spatial sample that can legitimately miss
            // the globe for ordinary camera geometries (e.g. wide FOV with
            // the globe filling most but not all of the screen), which
            // would otherwise make the requested zoom level swing wildly.
            // Visible bounds still decide which quadtree region to select,
            // just not how much detail to request.
            zoom_level = CalculateOptimalZoom(camera_geodetic->ellipsoid_height_meters);
            const BoundingBox2D visible_bounds = CalculateVisibleGeographicBounds(
                view_matrix, projection_matrix);

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
                        visible_region = {
                            visible_bounds.min.x,
                            visible_bounds.min.y,
                            visible_bounds.max.x,
                            visible_bounds.max.y,
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
                    // This startup-only legacy request API accepts degrees;
                    // the ECEF ray path above owns radians, so make that
                    // projection boundary explicit instead of mixing units.
                    const BoundingBox2D legacy_degrees_bounds(
                        glm::dvec2(constants::conversion::RadiansToDegrees(visible_bounds.min.x),
                                   constants::conversion::RadiansToDegrees(visible_bounds.min.y)),
                        glm::dvec2(constants::conversion::RadiansToDegrees(visible_bounds.max.x),
                                   constants::conversion::RadiansToDegrees(visible_bounds.max.y)));
                    const std::vector<TileCoordinates> candidate_tiles =
                        TileMathematics::GetTilesInBounds(legacy_degrees_bounds, zoom_level);

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
                tile_state.lod_level = CalculateTileLOD(tile_coords);
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

            // geographic_patch_geometry_cache_ is frame-independent (ECEF)
            // and persists across frames -- UpdateGeographicPatchDraws
            // prunes it to whatever's still needed, it is not cleared here.
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
        spdlog::info("Tile renderer config updated: max_tiles={}, max_uploads={}, fragment_probe={}",
                     config_.max_visible_tiles,
                     config_.max_tile_uploads_per_frame,
                     TileFragmentProbeName(config_.fragment_shading_probe));
    }

    void ClearCache() override {
        visible_tiles_.clear();
        geographic_patch_draws_.clear();
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

    void SetWireframeEnabled(bool enabled) override {
        wireframe_enabled_ = enabled;
    }

private:
    [[nodiscard]] glm::vec3 CameraRelativeRenderDirection(
        const glm::dvec3& ecef_direction) const {
        if (!render_frame_.has_value()) {
            return glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::normalize(glm::vec3(render_frame_->ToLocalDirection(ecef_direction)));
    }

    const std::vector<GeographicPatchVertexEcef>* GetOrCreateGeographicPatchGeometry(
        const renderer::GeographicQuadtreePatch& patch) {
        const auto found = geographic_patch_geometry_cache_.find(patch.imagery_key);
        if (found != geographic_patch_geometry_cache_.end()) {
            return &found->second;
        }
        if (!geographic_patch_grid_.has_value()) {
            return nullptr;
        }

        // Ellipsoid geometry depends only on the patch's own geographic
        // bounds, never on the camera -- cache it in ECEF so it survives
        // camera movement. The per-frame camera-relative conversion happens
        // separately, in UpdateGeographicPatchDraws.
        std::vector<GeographicPatchVertexEcef> vertices;
        vertices.reserve(geographic_patch_grid_->local_coordinates.size());
        for (const glm::vec2& local_uv_float : geographic_patch_grid_->local_coordinates) {
            const glm::dvec2 local_uv(local_uv_float.x, local_uv_float.y);
            const auto geodetic = renderer::PatchLocalToGeodetic(patch, local_uv);
            if (!geodetic.has_value()) {
                return nullptr;
            }
            vertices.push_back({
                geodesy::Wgs84Ellipsoid::ToEcef(*geodetic).meters,
                geodesy::Wgs84Ellipsoid::SurfaceNormal(*geodetic),
                local_uv_float,
            });
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

        std::vector<renderer::ResolvedImageryPatch> resolved_patches;
        resolved_patches.reserve(visible_tile_coords.size());

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

            resolved_patches.push_back(*resolved);
        }

        // The ECEF geometry cache is keyed by patch identity and nothing
        // else prunes it -- drop entries for patches not selected this
        // frame so it can't grow unbounded as the camera moves around.
        {
            std::unordered_set<imagery::ImageTileKey, imagery::ImageTileKeyHash> needed_keys;
            needed_keys.reserve(resolved_patches.size());
            for (const renderer::ResolvedImageryPatch& resolved : resolved_patches) {
                needed_keys.insert(resolved.patch.imagery_key);
            }
            for (auto it = geographic_patch_geometry_cache_.begin();
                 it != geographic_patch_geometry_cache_.end();) {
                it = needed_keys.count(it->first) == 0
                         ? geographic_patch_geometry_cache_.erase(it)
                         : std::next(it);
            }
        }

        // Unlike the ECEF cache above, this projection into camera-relative
        // floats must be rebuilt every frame regardless of whether the
        // patch set changed: render_frame_'s origin is the camera's
        // *current* position, so a buffer built for an earlier frame no
        // longer represents the same points relative to it. This is still
        // cheap -- a subtract-and-cast per vertex, not the ellipsoid math
        // above, which stays cached. atlas_uv/texture_layer are baked in
        // here too (from this frame's resolved imagery, which -- unlike the
        // geometry -- can legitimately change frame to frame as residency
        // changes), so no per-draw uniform update is needed at render time.
        geographic_patch_vertices_.clear();
        geographic_patch_vertex_offsets_.clear();
        geographic_patch_vertices_.reserve(
            resolved_patches.size() * geographic_patch_grid_->local_coordinates.size());

        for (const renderer::ResolvedImageryPatch& resolved : resolved_patches) {
            const imagery::ImageTileKey& key = resolved.patch.imagery_key;
            const auto geometry = geographic_patch_geometry_cache_.find(key);
            if (geometry == geographic_patch_geometry_cache_.end()) {
                continue;
            }
            geographic_patch_vertex_offsets_.emplace(
                key, geographic_patch_vertices_.size());
            const glm::vec2 uv_scale(static_cast<float>(resolved.uv_scale.x),
                                     static_cast<float>(resolved.uv_scale.y));
            const glm::vec2 uv_offset(static_cast<float>(resolved.uv_offset.x),
                                      static_cast<float>(resolved.uv_offset.y));
            const float texture_layer = static_cast<float>(resolved.texture_layer);
            for (const GeographicPatchVertexEcef& ecef_vertex : geometry->second) {
                // ecef_normal is already unit length (Wgs84Ellipsoid::
                // SurfaceNormal), and ToLocalDirection is a pure rotation
                // (the ENU basis is orthonormal) -- it preserves length, so
                // re-normalizing here would just be a wasted sqrt per vertex.
                geographic_patch_vertices_.push_back({
                    glm::vec3(render_frame_->ToLocal(
                        geodesy::EcefPosition{ecef_vertex.ecef_position})),
                    glm::vec3(render_frame_->ToLocalDirection(ecef_vertex.ecef_normal)),
                    ecef_vertex.local_uv,
                    uv_offset + ecef_vertex.local_uv * uv_scale,
                    texture_layer,
                });
            }
        }
        geographic_patch_vertices_dirty_ = true;

        geographic_patch_draws_.reserve(resolved_patches.size());
        for (const renderer::ResolvedImageryPatch& resolved : resolved_patches) {
            const auto offset = geographic_patch_vertex_offsets_.find(
                resolved.patch.imagery_key);
            if (offset == geographic_patch_vertex_offsets_.end()) {
                continue;
            }
            geographic_patch_draws_.push_back({resolved, offset->second});
        }
    }

    TileRenderConfig config_;
    TileTextureCoordinator* texture_coordinator_ = nullptr;
    std::optional<renderer::EcefRenderFrame> render_frame_;
    bool initialized_ = false;
    bool wireframe_enabled_ = false;
    std::uint64_t frame_counter_ = 0;
    std::vector<TileRenderState> visible_tiles_;
    std::optional<renderer::GeographicPatchGrid> geographic_patch_grid_;
    std::vector<GeographicPatchDraw> geographic_patch_draws_;
    std::unordered_map<imagery::ImageTileKey,
                       std::vector<GeographicPatchVertexEcef>,
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

    struct GeographicPatchUniformLocations {
        GLint view = -1;
        GLint projection = -1;
        GLint tile_pool = -1;
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
    std::uint32_t geographic_patch_vao_ = 0;
    std::uint32_t geographic_patch_vbo_ = 0;
    std::uint32_t geographic_patch_ebo_ = 0;

    // Coarse base-globe vertex shader.
    // Geographic patches receive already projected CPU geometry and a direct
    // physical texture-array layer. No fragment ray construction, Mercator
    // conversion, GPU lookup, or parent fallback occurs here.
    static constexpr const char* kGeographicPatchVertexShader = EARTH_MAP_GLSL_PREAMBLE R"(
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aLocalUv;
layout (location = 3) in vec2 aAtlasUv;
layout (location = 4) in float aTextureLayer;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 WorldPosition;
out vec3 WorldNormal;
out vec2 LocalUv;
out vec2 AtlasUv;
out float TextureLayer;

void main() {
    WorldPosition = aPosition;
    WorldNormal = aNormal;
    LocalUv = aLocalUv;
    AtlasUv = aAtlasUv;
    TextureLayer = aTextureLayer;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
)";

    static constexpr const char* kGeographicPatchFragmentShaderBody = R"(
#ifndef EARTH_MAP_TILE_FRAGMENT_PROBE
#define EARTH_MAP_TILE_FRAGMENT_PROBE 0
#endif

in vec3 WorldPosition;
in vec3 WorldNormal;
in vec2 LocalUv;
in vec2 AtlasUv;
in float TextureLayer;

out vec4 FragColor;

uniform sampler2DArray uTilePool;
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
    vec2 uv = clamp(AtlasUv, kHalfTexel, 1.0 - kHalfTexel);
    vec4 texColor = texture(uTilePool, vec3(uv, TextureLayer));

 #if EARTH_MAP_TILE_FRAGMENT_PROBE == 3
    FragColor = texColor;
 #else
    const float ambientStrength = 0.25;
    float diffuse = max(dot(normalize(WorldNormal), uLightDirection), 0.0);
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

    void CacheGeographicPatchUniformLocations(GeographicPatchProgramState& state) {
        const std::uint32_t program = state.program;
        GeographicPatchUniformLocations& uniform_locs = state.uniform_locs;
        uniform_locs.view = glGetUniformLocation(program, "uView");
        uniform_locs.projection = glGetUniformLocation(program, "uProjection");
        uniform_locs.tile_pool = glGetUniformLocation(program, "uTilePool");
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
                     "(camera-relative ECEF/ENU patch geometry)",
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
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              sizeof(GeographicPatchVertex),
                              reinterpret_cast<const void*>(offsetof(
                                  GeographicPatchVertex, normal)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                              sizeof(GeographicPatchVertex),
                              reinterpret_cast<const void*>(offsetof(
                                  GeographicPatchVertex, local_uv)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE,
                              sizeof(GeographicPatchVertex),
                              reinterpret_cast<const void*>(offsetof(
                                  GeographicPatchVertex, atlas_uv)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,
                              sizeof(GeographicPatchVertex),
                              reinterpret_cast<const void*>(offsetof(
                                  GeographicPatchVertex, texture_layer)));
        glEnableVertexAttribArray(4);
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
        const glm::vec3 local_light_direction = CameraRelativeRenderDirection(
            glm::dvec3(kDefaultSunDirection));
        glUniform3f(uniform_locs.light_direction,
                    local_light_direction.x, local_light_direction.y,
                    local_light_direction.z);
        glUniform3f(uniform_locs.light_color,
                    1.0f, 1.0f, 1.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY,
                      texture_coordinator_->GetTilePoolTextureID());
        glUniform1i(uniform_locs.tile_pool, 0);

        if (wireframe_enabled_) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        glBindVertexArray(geographic_patch_vao_);

        // texture_layer/atlas_uv are baked per-vertex now (see
        // UpdateGeographicPatchDraws), so unlike before, no per-patch
        // uniform update is needed here at all -- only the vertex range
        // differs per patch.
#ifdef __ANDROID__
        // GLES 3.0 core (this project's Android baseline) has no
        // glDrawElementsBaseVertex -- try the optional extension most real
        // devices actually advertise first (see ResolveDrawElementsBaseVertex
        // above), and only fall back to re-specifying the vertex attribute
        // pointers per patch if truly unavailable.
        if (const DrawElementsBaseVertexFn draw_base_vertex = GetDrawElementsBaseVertex()) {
            for (const GeographicPatchDraw& draw : geographic_patch_draws_) {
                draw_base_vertex(GL_TRIANGLES,
                                 static_cast<GLsizei>(geographic_patch_grid_->indices.size()),
                                 GL_UNSIGNED_INT,
                                 nullptr,
                                 static_cast<GLint>(draw.vertex_offset));
                draw_zone.AddDrawCall(geographic_patch_grid_->indices.size() / 3U);
            }
        } else {
            glBindBuffer(GL_ARRAY_BUFFER, geographic_patch_vbo_);
            for (const GeographicPatchDraw& draw : geographic_patch_draws_) {
                const std::size_t byte_offset =
                    draw.vertex_offset * sizeof(GeographicPatchVertex);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                      sizeof(GeographicPatchVertex),
                                      reinterpret_cast<const void*>(
                                          byte_offset + offsetof(GeographicPatchVertex, position)));
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                                      sizeof(GeographicPatchVertex),
                                      reinterpret_cast<const void*>(
                                          byte_offset + offsetof(GeographicPatchVertex, normal)));
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                                      sizeof(GeographicPatchVertex),
                                      reinterpret_cast<const void*>(
                                          byte_offset + offsetof(GeographicPatchVertex, local_uv)));
                glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE,
                                      sizeof(GeographicPatchVertex),
                                      reinterpret_cast<const void*>(
                                          byte_offset + offsetof(GeographicPatchVertex, atlas_uv)));
                glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,
                                      sizeof(GeographicPatchVertex),
                                      reinterpret_cast<const void*>(
                                          byte_offset + offsetof(GeographicPatchVertex, texture_layer)));
                glDrawElements(GL_TRIANGLES,
                               geographic_patch_grid_->indices.size(),
                               GL_UNSIGNED_INT,
                               nullptr);
                draw_zone.AddDrawCall(geographic_patch_grid_->indices.size() / 3U);
            }
        }
#else
        // Desktop GL (3.2+, guaranteed by GLEW's core profile here) can
        // select each patch's vertex base directly via basevertex, with the
        // attribute pointers bound once at init time (InitializeGeographic-
        // PatchGeometry) and never touched again -- no per-patch state
        // change at all beyond the draw call itself.
        for (const GeographicPatchDraw& draw : geographic_patch_draws_) {
            glDrawElementsBaseVertex(GL_TRIANGLES,
                                     geographic_patch_grid_->indices.size(),
                                     GL_UNSIGNED_INT,
                                     nullptr,
                                     static_cast<GLint>(draw.vertex_offset));
            draw_zone.AddDrawCall(geographic_patch_grid_->indices.size() / 3U);
        }
#endif

        if (wireframe_enabled_) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        glBindVertexArray(0);
    }

    void RenderDirectGeographicImagery(const glm::mat4& view_matrix,
                                        const glm::mat4& projection_matrix,
                                        FrameZoneScope& draw_zone) {
        // The geographic patch is the globe surface -- and, unlike the old
        // normalized icosphere this replaced, it is not one continuous mesh
        // but many independent per-patch draws. Depth testing must stay on
        // so overlapping/near-side patches (and anything else sharing this
        // depth buffer, e.g. elevation or placemarks) resolve correctly.
        RenderGeographicPatches(view_matrix, projection_matrix, draw_zone);
        // return;
        stats_.rendered_tiles = geographic_patch_draws_.size();
    }

    void Cleanup() {
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
    int CalculateOptimalZoom(double altitude_meters) const {
        if (altitude_meters <= 0.0) {
            return kMaxZoom;
        }
        const double zoom = std::log2(kZoomAltitudeScale / altitude_meters);
        return std::clamp(static_cast<int>(zoom), kMinZoom, kMaxZoom);
    }

    BoundingBox2D CalculateVisibleGeographicBounds(
        const glm::mat4& view_matrix,
        const glm::mat4& projection_matrix) const {
        if (!render_frame_.has_value()) {
            return BoundingBox2D(glm::dvec2(-constants::math::PI, -glm::half_pi<double>()),
                                 glm::dvec2(constants::math::PI, glm::half_pi<double>()));
        }

        const glm::mat4 inverse_view_projection = glm::inverse(projection_matrix * view_matrix);
        constexpr std::array<glm::vec2, 9> kSamplePoints = {{
            {0.5f, 0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},
            {0.5f, 0.0f}, {0.5f, 1.0f}, {0.0f, 0.5f}, {1.0f, 0.5f},
        }};

        double minimum_longitude = std::numeric_limits<double>::infinity();
        double maximum_longitude = -std::numeric_limits<double>::infinity();
        double minimum_latitude = std::numeric_limits<double>::infinity();
        double maximum_latitude = -std::numeric_limits<double>::infinity();
        int hit_count = 0;
        for (const glm::vec2 sample : kSamplePoints) {
            const float ndc_x = sample.x * 2.0f - 1.0f;
            const float ndc_y = sample.y * 2.0f - 1.0f;
            glm::vec4 near_point = inverse_view_projection * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
            glm::vec4 far_point = inverse_view_projection * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);
            near_point /= near_point.w;
            far_point /= far_point.w;
            const geodesy::EcefPosition near_ecef = render_frame_->FromLocal(glm::dvec3(near_point));
            const geodesy::EcefPosition far_ecef = render_frame_->FromLocal(glm::dvec3(far_point));
            const glm::dvec3 direction = glm::normalize(far_ecef.meters - near_ecef.meters);
            const auto intersection = geodesy::Wgs84Ellipsoid::IntersectRay(
                render_frame_->CameraOrigin(), direction);
            if (!intersection.has_value()) {
                continue;
            }
            const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(*intersection);
            if (!geodetic.has_value()) {
                continue;
            }
            ++hit_count;
            minimum_longitude = std::min(minimum_longitude, geodetic->longitude_radians);
            maximum_longitude = std::max(maximum_longitude, geodetic->longitude_radians);
            minimum_latitude = std::min(minimum_latitude, geodetic->latitude_radians);
            maximum_latitude = std::max(maximum_latitude, geodetic->latitude_radians);
        }

        // The ray-cast box above is only exact when every sample hits the
        // ellipsoid -- i.e. the globe fills the entire viewport. The moment
        // even one sample ray misses, the horizon lies inside the viewport
        // somewhere, and the hits we *did* get no longer bound the true
        // visible region: they just describe whichever arbitrary subset of
        // the 9 probes happened to land on the globe. Trusting that partial
        // subset yields a plausible-looking but wrong (too small, off-centre)
        // box -- this was the "isolated floating tile" failure. When that
        // happens, fall back to a closed-form estimate: the geocentric
        // visible cap centred on the camera's own sub-point.
        //
        // Geometry: for an observer at distance d from Earth's centre, the
        // horizon is where the line of sight is tangent to the sphere of
        // radius R. The tangent line, the radius to the tangent point, and
        // the line from observer to centre form a right triangle with the
        // right angle at the tangent point, so cos(half_angle) = R / d --
        // NOT asin(R / d), which gives the sphere's apparent angular size as
        // seen by the eye, a different quantity. Sanity check: at d == R the
        // observer is on the surface and half_angle -> 0 (a point); as
        // d -> infinity, half_angle -> 90 degrees (a full hemisphere).
        if (hit_count < static_cast<int>(kSamplePoints.size())) {
            const geodesy::EcefPosition camera_origin = render_frame_->CameraOrigin();
            const double distance_from_center = glm::length(camera_origin.meters);
            const auto camera_geodetic = geodesy::Wgs84Ellipsoid::FromEcef(camera_origin);
            if (distance_from_center > geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters &&
                camera_geodetic.has_value()) {
                const double half_angle = std::acos(std::clamp(
                    geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters / distance_from_center,
                    -1.0, 1.0));
                minimum_longitude = camera_geodetic->longitude_radians - half_angle;
                maximum_longitude = camera_geodetic->longitude_radians + half_angle;
                minimum_latitude = std::max(camera_geodetic->latitude_radians - half_angle,
                                            -glm::half_pi<double>());
                maximum_latitude = std::min(camera_geodetic->latitude_radians + half_angle,
                                            glm::half_pi<double>());
            } else {
                // Camera at/below the surface, or the geodetic conversion
                // failed -- there is no well-defined horizon cap. Treat the
                // whole globe as potentially visible rather than guess.
                minimum_longitude = -constants::math::PI;
                maximum_longitude = constants::math::PI;
                minimum_latitude = -glm::half_pi<double>();
                maximum_latitude = glm::half_pi<double>();
            }
        }

        if (!std::isfinite(minimum_longitude) ||
            minimum_longitude < -constants::math::PI ||
            maximum_longitude > constants::math::PI ||
            maximum_longitude - minimum_longitude > glm::pi<double>()) {
            minimum_longitude = -constants::math::PI;
            maximum_longitude = constants::math::PI;
        }
        if (!std::isfinite(minimum_latitude)) {
            minimum_latitude = -glm::half_pi<double>();
            maximum_latitude = glm::half_pi<double>();
        }
        return BoundingBox2D({minimum_longitude, minimum_latitude},
                             {maximum_longitude, maximum_latitude});
    }

    float CalculateTileLOD(const TileCoordinates& tile) const {
        return static_cast<float>(tile.zoom);
    }

    float CalculateLoadPriority(const TileCoordinates& tile,
                                const geodesy::EcefPosition& /*camera_position*/) const {
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
