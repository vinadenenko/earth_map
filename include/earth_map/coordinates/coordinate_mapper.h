/**
 * @file coordinate_mapper.h
 * @brief Centralized coordinate system conversion hub
 *
 * This is THE ONLY place where coordinate conversions happen.
 * All conversions between coordinate spaces must go through this class.
 *
 * Design Principles:
 * - Single Source of Truth: All conversion logic centralized here
 * - Type Safety: Explicit types prevent coordinate space mixing
 * - Stateless: All methods are static (pure functions)
 * - Testable: Each conversion can be unit tested independently
 * - Explicit: No implicit conversions
 *
 * @see COORDINATE_SYSTEM_ARCHITECTURE_PLAN.md for architecture details
 */

#pragma once

#include <earth_map/coordinates/coordinate_spaces.h>
#include <earth_map/math/tile_mathematics.h>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace earth_map {
namespace coordinates {

/**
 * @brief Central hub for all coordinate space conversions
 *
 * This class provides the ONLY interface for converting between different
 * coordinate systems in the GIS application. All conversions are explicit
 * and type-safe.
 *
 * Coordinate System Conventions:
 * - World: Globe radius=1.0, Y+=North, Z+=lon 0°, X+=lon 90°E
 * - Geographic: WGS84 (lat∈[-90,90], lon∈[-180,180], alt in meters)
 * - Projected: Web Mercator EPSG:3857 (x,y in meters)
 * - Tile: Slippy map tiles (x, y, zoom)
 * - Screen: Pixels from top-left (x+ right, y+ down)
 */
class CoordinateMapper {
public:
    // ========================================================================
    // GEOGRAPHIC ↔ WORLD (3D Globe Rendering)
    // ========================================================================

    /**
     * @brief Convert geographic coordinates to 3D world position on sphere
     *
     * Convention:
     * - (lat=0°, lon=0°) → (x=0, y=0, z=radius) [Prime meridian, equator]
     * - (lat=90°, lon=*) → (x=0, y=radius, z=0) [North pole]
     * - (lat=0°, lon=90°) → (x=radius, y=0, z=0) [90° East]
     *
     * @param geo Geographic coordinates (lat, lon, alt)
     * @param radius Globe radius in world units (default: 1.0)
     * @return 3D position on sphere surface
     */
    [[nodiscard]] static World GeographicToWorld(const Geographic& geo,
                                                  float radius = 1.0f) noexcept;

    /**
     * @brief Convert 3D world position to geographic coordinates
     *
     * Projects position onto sphere and calculates lat/lon.
     * Altitude is set to distance from sphere surface.
     *
     * @param world Position in world space
     * @param radius Globe radius (default: 1.0)
     * @return Geographic coordinates
     */
    [[nodiscard]] static Geographic WorldToGeographic(const World& world,
                                                       float radius = 1.0f) noexcept;

    // ========================================================================
    // GEOGRAPHIC ↔ PROJECTED (Web Mercator)
    // ========================================================================

    /**
     * @brief Project geographic to Web Mercator
     *
     * Uses Web Mercator projection (EPSG:3857):
     * - Valid latitude range: [-85.05112877980660°, +85.05112877980660°]
     * - Output range: [-20037508.34m, +20037508.34m] in both axes
     *
     * @param geo Geographic coordinates
     * @return Projected coordinates in meters, or invalid if out of range
     */
    [[nodiscard]] static Projected GeographicToProjected(const Geographic& geo) noexcept;

    /**
     * @brief Unproject Web Mercator to geographic
     *
     * Inverse of Web Mercator projection.
     *
     * @param proj Projected coordinates in meters
     * @return Geographic coordinates
     */
    [[nodiscard]] static Geographic ProjectedToGeographic(const Projected& proj) noexcept;

    // ========================================================================
    // GEOGRAPHIC ↔ TILE (Slippy Map Tiles)
    // ========================================================================

    /**
     * @brief Convert geographic point to tile coordinates
     *
     * Uses standard slippy map tile addressing:
     * - Tile (0,0) at zoom Z is northwest corner (lat=85.05°, lon=-180°)
     * - At zoom Z, world is divided into 2^Z × 2^Z tiles
     * - Each tile is 256×256 pixels
     *
     * @param geo Geographic position
     * @param zoom Zoom level (0-30)
     * @return Tile coordinates containing this point
     */
    [[nodiscard]] static TileCoordinates GeographicToTile(const Geographic& geo,
                                                           int32_t zoom);

    /**
     * @brief Get geographic bounds of a tile
     *
     * Returns the geographic extent of a tile in degrees.
     *
     * @param tile Tile coordinates (x, y, zoom)
     * @return Geographic bounding box of tile
     */
    [[nodiscard]] static GeographicBounds TileToGeographic(const TileCoordinates& tile);

    /**
     * @brief Get all tiles covering a geographic region
     *
     * Finds all tiles at specified zoom level that intersect the given bounds.
     *
     * @param bounds Geographic bounding box
     * @param zoom Zoom level
     * @return Vector of tile coordinates covering the region
     */
    [[nodiscard]] static std::vector<TileCoordinates> GeographicBoundsToTiles(
        const GeographicBounds& bounds,
        int32_t zoom);

    /**
     * @brief Convert geographic to tile coordinates using SPHERICAL mapping (3D globe)
     *
     * Uses LINEAR latitude/longitude mapping for 3D sphere rendering:
     * - NOT Web Mercator! This is for 3D globe only.
     * - Latitude [-90°, 90°] → Y [0, 2^zoom] (linear, no distortion)
     * - Longitude [-180°, 180°] → X [0, 2^zoom] (linear wraparound)
     *
     * This mapping ensures tile coordinates match sphere vertex positions.
     * For 2D minimap, use Web Mercator-based GeographicToTile() instead.
     *
     * @param geo Geographic coordinates
     * @param zoom Zoom level (0-30)
     * @return Tile coordinates using spherical projection
     */
    [[nodiscard]] static TileCoordinates GeographicToSphericalTile(
        const Geographic& geo,
        int32_t zoom) noexcept;

    /**
     * @brief Calculate fractional position within a tile (0-1 range)
     *
     * Given a geographic point and its containing tile, calculate the
     * fractional position within that tile for texture coordinate interpolation.
     *
     * @param geo Geographic coordinates
     * @param tile Tile containing this point
     * @return Fractional position (x, y) in [0, 1] within tile
     */
    [[nodiscard]] static glm::vec2 GetTileFraction(
        const Geographic& geo,
        const TileCoordinates& tile) noexcept;

    // ========================================================================
    // GEOGRAPHIC ↔ SCREEN (User Interaction)
    // ========================================================================

    /**
     * @brief Project geographic point to screen coordinates
     *
     * Uses OpenGL projection pipeline: World → Camera → Projection → Viewport.
     * Returns nullopt if point is behind camera or outside viewport.
     *
     * @param geo Geographic position
     * @param view_matrix Camera view matrix
     * @param proj_matrix Projection matrix (perspective/orthographic)
     * @param viewport Screen viewport (x, y, width, height)
     * @return Screen coordinates in pixels, or nullopt if not visible
     */
    [[nodiscard]] static std::optional<Screen> GeographicToScreen(
        const Geographic& geo,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        const glm::ivec4& viewport) noexcept;

    /**
     * @brief Unproject screen point to geographic coordinates
     *
     * Casts ray from camera through screen point and intersects with globe.
     * Returns nullopt if ray doesn't hit globe.
     *
     * @param screen Screen coordinates (pixel position)
     * @param view_matrix Camera view matrix
     * @param proj_matrix Projection matrix
     * @param viewport Screen viewport
     * @param globe_radius Radius of globe to intersect (default: 1.0)
     * @return Geographic coordinates, or nullopt if no intersection
     */
    [[nodiscard]] static std::optional<Geographic> ScreenToGeographic(
        const Screen& screen,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        const glm::ivec4& viewport,
        float globe_radius = 1.0f) noexcept;

    // ========================================================================
    // WORLD ↔ SCREEN (Internal Rendering)
    // ========================================================================

    /**
     * @brief Project 3D world position to screen
     *
     * Direct transformation using OpenGL pipeline.
     * Used internally by GeographicToScreen.
     *
     * @param world World space position
     * @param view_matrix Camera view matrix
     * @param proj_matrix Projection matrix
     * @param viewport Screen viewport
     * @return Screen coordinates, or nullopt if behind camera
     */
    [[nodiscard]] static std::optional<Screen> WorldToScreen(
        const World& world,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        const glm::ivec4& viewport) noexcept;

    /**
     * @brief Unproject screen to ray in world space
     *
     * Creates ray from camera through screen point for ray-casting.
     *
     * @param screen Screen coordinates
     * @param view_matrix Camera view matrix (must be invertible)
     * @param proj_matrix Projection matrix (must be invertible)
     * @param viewport Screen viewport
     * @return {ray_origin, ray_direction} in world space
     */
    [[nodiscard]] static std::pair<World, glm::vec3> ScreenToWorldRay(
        const Screen& screen,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        const glm::ivec4& viewport) noexcept;

    // ========================================================================
    // UTILITY: Bounds Conversions
    // ========================================================================

    /**
     * @brief Calculate visible geographic bounds from camera
     *
     * Determines which geographic region is visible in the current view.
     * Useful for tile selection and level-of-detail calculations.
     *
     * @param camera_world Camera position in world space
     * @param view_matrix View matrix
     * @param proj_matrix Projection matrix
     * @param globe_radius Radius of globe (default: 1.0)
     * @return Geographic bounding box of visible area
     */
    [[nodiscard]] static GeographicBounds CalculateVisibleGeographicBounds(
        const World& camera_world,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        float globe_radius = 1.0f) noexcept;

    // ========================================================================
    // Low-Level Cartesian ↔ Geographic Conversions
    // ========================================================================

    /**
     * @brief Convert geographic to Cartesian coordinates (low-level)
     *
     * Converts lat/lon to 3D Cartesian coordinates using spherical coordinate system.
     *
     * @param geo Geographic coordinates (lat, lon, altitude)
     * @param radius Sphere radius (default: 1.0 for normalized coordinates)
     * @return 3D Cartesian position
     *
     * @note For most use cases, prefer GeographicToWorld() which returns a typed World coordinate.
     *       This low-level function is useful when working directly with raw vertices/positions.
     */
    [[nodiscard]] static glm::vec3 GeographicToCartesian(const Geographic& geo,
                                                          float radius = 1.0f) noexcept;

    /**
     * @brief Convert Cartesian to geographic coordinates (low-level)
     *
     * Converts 3D Cartesian position to lat/lon using inverse spherical coordinate system.
     *
     * @param cartesian 3D position (will be normalized to unit sphere)
     * @return Geographic coordinates (altitude will be 0)
     *
     * @note For most use cases, prefer WorldToGeographic() which accepts a typed World coordinate.
     *       This low-level function is useful when working directly with raw vertices/positions.
     */
    [[nodiscard]] static Geographic CartesianToGeographic(const glm::vec3& cartesian) noexcept;

private:
    // ========================================================================
    // Internal Helper Functions
    // ========================================================================

    /**
     * @brief Ray-sphere intersection test
     * @param ray_origin Ray starting point
     * @param ray_dir Ray direction (should be normalized)
     * @param sphere_center Sphere center
     * @param sphere_radius Sphere radius
     * @param[out] intersection_point Intersection point if found
     * @return true if ray intersects sphere
     */
    [[nodiscard]] static bool RaySphereIntersection(
        const glm::vec3& ray_origin,
        const glm::vec3& ray_dir,
        const glm::vec3& sphere_center,
        float sphere_radius,
        glm::vec3& intersection_point) noexcept;
};

} // namespace coordinates
} // namespace earth_map
