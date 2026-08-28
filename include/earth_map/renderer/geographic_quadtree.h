/**
 * @file geographic_quadtree.h
 * @brief Geographic imagery patches and CPU virtual-texture resolution.
 *
 * A geographic quadtree patch is the rendering unit for globe imagery.  Its
 * integer address is an imagery tile-matrix address, while its physical
 * bounds are WGS84 geodetic radians.  This separation is deliberate:
 * Web Mercator describes the source imagery layout, never the shape of Earth.
 *
 * The renderer will select patches on the CPU, resolve the best resident
 * virtual-texture page once per patch, and issue direct texture-array draws.
 * The fragment shader therefore receives only a layer and local UV transform;
 * it does not perform a ray cast, a Mercator projection, or a GPU lookup walk.
 */

#pragma once

#include <earth_map/geodesy/wgs84_ellipsoid.h>
#include <earth_map/imagery/tile_matrix_set.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace earth_map::renderer {

/** Geographic WGS84-radian bounds of one imagery quadtree patch. */
struct GeographicPatchBounds final {
    double west_longitude_radians = 0.0;
    double south_latitude_radians = 0.0;
    double east_longitude_radians = 0.0;
    double north_latitude_radians = 0.0;

    [[nodiscard]] bool IsValid() const noexcept;
};

/**
 * CPU-side representation of one geographic quadtree leaf.
 *
 * `imagery_key` is source-aware and is also the request/cache identity.  It
 * must be produced by a TileProvider/TileMatrixSet rather than fabricated
 * from a renderer-local zoom and X/Y convention.
 */
struct GeographicQuadtreePatch final {
    imagery::ImageTileKey imagery_key;
    GeographicPatchBounds bounds;
    imagery::TileRowOrder row_order = imagery::TileRowOrder::NorthToSouth;
};

/**
 * Resolves a selected leaf patch to one resident texture-array layer.
 *
 * The resident page may be an ancestor of the requested leaf.  `uv_scale`
 * and `uv_offset` map the leaf's local [0, 1] UV into that ancestor page.
 * This is entirely integer quadtree math; it is stable at every zoom level.
 */
struct ResolvedImageryPatch final {
    GeographicQuadtreePatch patch;
    imagery::ImageTileKey resident_key;
    std::uint16_t texture_layer = 0;
    std::uint32_t ancestor_levels = 0;
    glm::dvec2 uv_scale{1.0, 1.0};
    glm::dvec2 uv_offset{0.0, 0.0};
};

/** A reusable local-grid topology for all geographic patches. */
struct GeographicPatchGrid final {
    std::uint32_t subdivisions = 0;
    std::vector<glm::vec2> local_coordinates;
    std::vector<std::uint32_t> indices;
};

/**
 * Input for the source-aware geographic-quadtree selector.
 *
 * A view crossing the anti-meridian is represented as two non-wrapping
 * geographic regions.  Keeping regions explicit avoids hiding longitude
 * wrapping in floating-point comparison code.
 */
struct GeographicQuadtreeSelectionConfig final {
    std::vector<GeographicPatchBounds> visible_regions;
    std::uint32_t target_level = 0;
    std::size_t maximum_leaf_count = 0;
};

/**
 * Builds a source-aware geographic patch from a declared imagery matrix.
 * Returns nullopt for an invalid key/address or a projection the patch
 * renderer does not support yet.
 */
[[nodiscard]] std::optional<GeographicQuadtreePatch> MakeGeographicQuadtreePatch(
    const imagery::TileMatrixSet& matrix_set,
    const imagery::ImageTileKey& imagery_key) noexcept;

/**
 * Converts a patch-local imagery UV to the physical WGS84 surface.
 *
 * Local V is always north-to-south, matching decoded image rows.  The source
 * matrix's row-order affects only the address-to-geographic conversion.
 */
[[nodiscard]] std::optional<geodesy::GeodeticPosition> PatchLocalToGeodetic(
    const GeographicQuadtreePatch& patch,
    const glm::dvec2& local_uv,
    double ellipsoid_height_meters = 0.0) noexcept;

/** Converts a patch-local imagery UV directly to WGS84 ECEF metres. */
[[nodiscard]] std::optional<geodesy::EcefPosition> PatchLocalToEcef(
    const GeographicQuadtreePatch& patch,
    const glm::dvec2& local_uv,
    double ellipsoid_height_meters = 0.0) noexcept;

/**
 * Finds the closest resident page for a selected leaf patch.
 *
 * `resident_layer` must return a physical texture-array layer only for a
 * currently resident page.  Page-table-window membership is intentionally
 * not involved: this is direct physical residency resolution on the CPU.
 */
[[nodiscard]] std::optional<ResolvedImageryPatch> ResolveResidentImageryPatch(
    const GeographicQuadtreePatch& patch,
    std::uint32_t max_ancestor_levels,
    const std::function<std::optional<std::uint16_t>(
        const imagery::ImageTileKey&)>& resident_layer);

/**
 * Builds an indexed [0,1]x[0,1] patch grid.  The topology is independent of
 * imagery zoom and can later carry terrain heights without changing imagery
 * page selection or fragment sampling.
 */
[[nodiscard]] std::optional<GeographicPatchGrid> MakeGeographicPatchGrid(
   std::uint32_t subdivisions) noexcept;

/**
 * Selects visible source-matrix leaves by refining a geographic quadtree.
 *
 * The selector never fabricates tile identities: every returned key carries
 * the caller's imagery source and the declared matrix-set ID.  When the
 * requested level would exceed `maximum_leaf_count`, it keeps the current
 * complete refinement frontier rather than dropping an arbitrary suffix of
 * tiles.  Those coarser leaves are valid imagery/terrain fallback patches.
 *
 * This is the deterministic hierarchy traversal used before the later SSE
 * policy supplies a target level.  The caller must pass one or two
 * non-wrapping regions; two regions represent an anti-meridian view.
 */
[[nodiscard]] std::vector<imagery::ImageTileKey>
SelectVisibleGeographicQuadtreeLeaves(
    const imagery::TileMatrixSet& matrix_set,
    std::string imagery_source_id,
    const GeographicQuadtreeSelectionConfig& config);

}  // namespace earth_map::renderer
