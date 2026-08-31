/**
 * @file point_placemark_renderer.h
 * @brief Private point-placemark renderer: icon billboards over a single
 * instanced draw call, regardless of placemark count.
 */

#pragma once

#include <earth_map/geodesy/wgs84_ellipsoid.h>
#include <earth_map/placemarks/placemark_icon_registry.h>
#include <earth_map/placemarks/placemark_layer.h>

#include "../../placemarks/placemark_selector.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace earth_map::renderer::placemarks {

/**
 * One GPU instance record for the point-placemark billboard shader.
 * local_position is camera-relative ENU (see renderer::EcefRenderFrame) --
 * frame-dependent, rebuilt every frame, same convention as
 * TileRenderer's geographic patch vertices.
 */
struct PointPlacemarkInstance final {
    glm::vec3 local_position{0.0f};
    glm::vec2 atlas_uv_min{0.0f};
    glm::vec2 atlas_uv_max{1.0f};
    float scale = 1.0f;
    float rotation_radians = 0.0f;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

/**
 * Renders visible point placemarks as screen-aligned icon billboards.
 *
 * Selection (frustum + WGS84-horizon culling) is delegated to the existing,
 * already-unit-tested placemarks::internal::PlacemarkSelector/
 * PlacemarkSpatialIndex -- this class only turns that selection into GPU
 * instance data and issues one glDrawArraysInstanced call per frame,
 * regardless of how many placemarks are visible.
 *
 * BuildInstances() is GL-free and exposed for unit testing; Render() adds
 * the GL atlas-texture/instance-buffer/draw-call layer on top.
 */
class PointPlacemarkRenderer final {
public:
    /** @param skip_gl_init Skip OpenGL resource creation (for unit tests). */
    explicit PointPlacemarkRenderer(bool skip_gl_init = false);
    ~PointPlacemarkRenderer();

    PointPlacemarkRenderer(const PointPlacemarkRenderer&) = delete;
    PointPlacemarkRenderer& operator=(const PointPlacemarkRenderer&) = delete;

    /**
     * Selects visible point placemarks and builds their instance records.
     * Camera-relative conversion depends only on camera_geodetic, not on
     * any prior frame's state -- safe to call every frame.
     */
    [[nodiscard]] std::vector<PointPlacemarkInstance> BuildInstances(
        const earth_map::placemarks::PlacemarkSnapshot& placemark_snapshot,
        const earth_map::placemarks::PlacemarkIconSnapshot& icon_snapshot,
        const geodesy::GeodeticPosition& camera_geodetic,
        const glm::mat4& view_matrix,
        const glm::mat4& projection_matrix);

    /**
     * Full per-frame render: builds instances, rebuilds the GL icon atlas
     * texture if the icon registry's revision changed, uploads the instance
     * buffer, and issues one instanced draw call.
     *
     * @param viewport_size_pixels Current viewport size, needed by the
     *   vertex shader to keep icons a constant size in screen pixels.
     */
    void Render(const earth_map::placemarks::PlacemarkSnapshot& placemark_snapshot,
                const earth_map::placemarks::PlacemarkIconSnapshot& icon_snapshot,
                const geodesy::GeodeticPosition& camera_geodetic,
                const glm::mat4& view_matrix,
                const glm::mat4& projection_matrix,
                const glm::vec2& viewport_size_pixels);

private:
    void EnsureGlResources();
    void EnsureAtlasTexture(const earth_map::placemarks::PlacemarkIconSnapshot& icon_snapshot);

    const bool skip_gl_init_;
    earth_map::placemarks::internal::PlacemarkSelector selector_;

    bool gl_resources_ready_ = false;
    std::uint32_t vao_ = 0;
    std::uint32_t quad_vbo_ = 0;
    std::uint32_t instance_vbo_ = 0;
    std::uint32_t program_ = 0;

    bool atlas_built_ = false;
    std::uint64_t atlas_revision_built_ = 0;
    std::uint32_t atlas_texture_ = 0;

    struct UniformLocations {
        std::int32_t view = -1;
        std::int32_t projection = -1;
        std::int32_t viewport_size = -1;
        std::int32_t atlas = -1;
    } uniform_locs_;
};

}  // namespace earth_map::renderer::placemarks
