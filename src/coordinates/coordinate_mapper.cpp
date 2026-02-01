/**
 * @file coordinate_mapper.cpp
 * @brief Implementation of centralized coordinate conversion system
 */

#include "../../include/earth_map/coordinates/coordinate_mapper.h"
#include "../../include/earth_map/math/projection.h"
#include "../../include/earth_map/math/tile_mathematics.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <limits>

namespace earth_map {
namespace coordinates {

// ============================================================================
// Geographic ↔ World Conversions
// ============================================================================

World CoordinateMapper::GeographicToWorld(const Geographic& geo, float radius) noexcept {
    if (!geo.IsValid()) {
        return World();
    }

    return World(GeographicToCartesian(geo, radius));
}

Geographic CoordinateMapper::WorldToGeographic(const World& world, float radius) noexcept {
    // Project position onto sphere of given radius
    glm::vec3 direction = world.Direction();
    glm::vec3 on_surface = direction * radius;

    Geographic geo = CartesianToGeographic(on_surface);

    // Calculate altitude as distance from sphere surface
    float actual_distance = world.Distance();
    geo.altitude = static_cast<double>(actual_distance - radius);

    return geo;
}

// ============================================================================
// Geographic ↔ Projected Conversions
// ============================================================================

Projected CoordinateMapper::GeographicToProjected(const Geographic& geo) noexcept {
    if (!geo.IsValid()) {
        // Return invalid projected coordinates (NaN)
        return Projected(
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()
        );
    }

    try {
        // Use new type-safe projection system
        auto web_mercator = std::static_pointer_cast<WebMercatorProjection>(
            ProjectionRegistry::GetProjection(ProjectionType::WEB_MERCATOR)
        );

        // Direct conversion - no legacy type adapters needed
        return web_mercator->Project(geo);
    }
    catch (const std::invalid_argument&) {
        // Out of Web Mercator bounds - return invalid
        return Projected(
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()
        );
    }
}

Geographic CoordinateMapper::ProjectedToGeographic(const Projected& proj) noexcept {
    if (!proj.IsValid()) {
        return Geographic();
    }

    try {
        auto web_mercator = std::static_pointer_cast<WebMercatorProjection>(
            ProjectionRegistry::GetProjection(ProjectionType::WEB_MERCATOR)
        );

        // Direct conversion - no legacy type adapters needed
        return web_mercator->Unproject(proj);
    }
    catch (const std::exception&) {
        return Geographic();
    }
}

// ============================================================================
// Geographic ↔ Tile Conversions
// ============================================================================

TileCoordinates CoordinateMapper::GeographicToTile(const Geographic& geo, int32_t zoom) {
    if (!geo.IsValid()) {
        return TileCoordinates();
    }

    return TileMathematics::GeographicToTile(geo, zoom);
}

GeographicBounds CoordinateMapper::TileToGeographic(const TileCoordinates& tile) {
    if (!tile.IsValid()) {
        return GeographicBounds();
    }

    // Use existing TileMathematics
    BoundingBox2D legacy_bounds = TileMathematics::GetTileBounds(tile);

    if (!legacy_bounds.IsValid()) {
        return GeographicBounds();
    }

    Geographic min(legacy_bounds.min.y, legacy_bounds.min.x, 0.0);
    Geographic max(legacy_bounds.max.y, legacy_bounds.max.x, 0.0);

    return GeographicBounds(min, max);
}

std::vector<TileCoordinates> CoordinateMapper::GeographicBoundsToTiles(
    const GeographicBounds& bounds,
    int32_t zoom) {

    if (!bounds.IsValid()) {
        return {};
    }

    // Convert to legacy BoundingBox2D format
    BoundingBox2D legacy_bounds(
        glm::dvec2(bounds.min.longitude, bounds.min.latitude),
        glm::dvec2(bounds.max.longitude, bounds.max.latitude)
    );

    return TileMathematics::GetTilesInBounds(legacy_bounds, zoom);
}

// ============================================================================
// Geographic ↔ Screen Conversions
// ============================================================================

std::optional<Screen> CoordinateMapper::GeographicToScreen(
    const Geographic& geo,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix,
    const glm::ivec4& viewport) noexcept {

    if (!geo.IsValid()) {
        return std::nullopt;
    }

    // Convert geographic to world space
    World world = GeographicToWorld(geo);

    // Project to screen
    return WorldToScreen(world, view_matrix, proj_matrix, viewport);
}

std::optional<Geographic> CoordinateMapper::ScreenToGeographic(
    const Screen& screen,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix,
    const glm::ivec4& viewport,
    float globe_radius) noexcept {

    if (!screen.IsValid()) {
        return std::nullopt;
    }

    // Get ray from screen point
    auto [ray_origin, ray_dir] = ScreenToWorldRay(screen, view_matrix, proj_matrix, viewport);

    // Intersect ray with sphere
    glm::vec3 intersection;
    bool hit = RaySphereIntersection(
        ray_origin.position,
        ray_dir,
        glm::vec3(0.0f, 0.0f, 0.0f),  // Sphere at origin
        globe_radius,
        intersection
    );

    if (!hit) {
        return std::nullopt;
    }

    // Convert intersection point to geographic
    return CartesianToGeographic(intersection);
}

// ============================================================================
// World ↔ Screen Conversions
// ============================================================================

std::optional<Screen> CoordinateMapper::WorldToScreen(
    const World& world,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix,
    const glm::ivec4& viewport) noexcept {

    // Transform to clip space
    glm::vec4 clip = proj_matrix * view_matrix * glm::vec4(world.position, 1.0f);

    // Check if behind camera (negative w after projection)
    if (clip.w <= 0.0f) {
        return std::nullopt;
    }

    // Perspective divide to NDC
    glm::vec3 ndc = glm::vec3(clip) / clip.w;

    // Check if outside NDC cube [-1, 1]
    if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f || std::abs(ndc.z) > 1.0f) {
        return std::nullopt;
    }

    // Transform to screen coordinates
    // NDC: [-1, 1] → Screen: [0, width/height]
    // Screen Y: 0 at bottom, viewport[3] at top (OpenGL convention after GLFW Y-flip)
    // NDC Y: -1 at bottom, +1 at top
    double screen_x = (ndc.x * 0.5 + 0.5) * viewport[2] + viewport[0];
    double screen_y = (ndc.y * 0.5 + 0.5) * viewport[3] + viewport[1];

    return Screen(screen_x, screen_y);
}

std::pair<World, glm::vec3> CoordinateMapper::ScreenToWorldRay(
    const Screen& screen,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix,
    const glm::ivec4& viewport) noexcept {

    // Compute inverse matrices
    glm::mat4 inv_proj = glm::inverse(proj_matrix);
    glm::mat4 inv_view = glm::inverse(view_matrix);

    // Camera position in world space
    glm::vec3 camera_pos = glm::vec3(inv_view[3]);

    // Convert screen to NDC
    // Screen Y: 0 at bottom, viewport[3] at top (after GLFW→OpenGL Y-flip)
    // NDC Y: -1 at bottom, +1 at top
    // Correct linear mapping: ndc_y = 2 * (screen.y / height) - 1
    float ndc_x = (2.0f * static_cast<float>(screen.x - viewport[0])) / viewport[2] - 1.0f;
    float ndc_y = (2.0f * static_cast<float>(screen.y - viewport[1])) / viewport[3] - 1.0f;

    // Unproject near plane point (NDC z = -1 maps to near plane)
    glm::vec4 near_clip(ndc_x, ndc_y, -1.0f, 1.0f);
    glm::vec4 near_view = inv_proj * near_clip;
    near_view /= near_view.w;  // Perspective divide
    glm::vec4 near_world_4 = inv_view * near_view;
    glm::vec3 near_world = glm::vec3(near_world_4) / near_world_4.w;

    // Unproject far plane point (NDC z = 1 maps to far plane)
    glm::vec4 far_clip(ndc_x, ndc_y, 1.0f, 1.0f);
    glm::vec4 far_view = inv_proj * far_clip;
    far_view /= far_view.w;  // Perspective divide
    glm::vec4 far_world_4 = inv_view * far_view;
    glm::vec3 far_world = glm::vec3(far_world_4) / far_world_4.w;

    // Ray direction from near to far
    glm::vec3 ray_dir = glm::normalize(far_world - near_world);

    return {World(camera_pos), ray_dir};
}

GeographicBounds GetEverestGeoBounds() {
    // Approximate bounding box around Mount Everest
    constexpr double min_lat_nepal = 20.0;
    constexpr double max_lat_nepal = 40.8;
    constexpr double min_lon_nepal = 79.5;
    constexpr double max_lon_nepal = 105.5;

    return GeographicBounds(
        Geographic(min_lat_nepal, min_lon_nepal, 0.0),
        Geographic(max_lat_nepal, max_lon_nepal, 0.0)
        );
}

// ============================================================================
// Utility: Bounds Conversions
// ============================================================================

GeographicBounds CoordinateMapper::CalculateVisibleGeographicBounds(
    const World& camera_world,
    const glm::mat4& view_matrix,  // Now used for accurate orientation-based calculation
    const glm::mat4& proj_matrix,
    float globe_radius) noexcept {

    // Do not remove, this is for elevation testing
    // return GetEverestGeoBounds();

    glm::vec3 camera_pos = camera_world.position;
    float distance = glm::length(camera_pos);

    // Create combined view-projection matrix for frustum corner extraction
    glm::mat4 view_proj = proj_matrix * view_matrix;

    // Attempt to invert the view-projection matrix for ray-casting
    glm::mat4 inv_vp;
    try {
        inv_vp = glm::inverse(view_proj);
    } catch (...) {
        // Fallback to old method if matrix is not invertible
        glm::vec3 view_point = glm::normalize(camera_pos) * globe_radius;
        Geographic center = CartesianToGeographic(view_point);

        float horizon_angle_rad = std::asin(std::min(1.0f, globe_radius / distance));
        float horizon_angle_deg = glm::degrees(horizon_angle_rad);
        float fov_y = 2.0f * std::atan(1.0f / proj_matrix[1][1]);
        float fov_deg = glm::degrees(fov_y);
        float coverage_angle = std::min(160.0f, std::max(horizon_angle_deg, fov_deg / 2.0f) * 2.0f);
        double half_coverage = static_cast<double>(coverage_angle) / 2.0;

        return GeographicBounds(
            Geographic(std::max(-85.05, center.latitude - half_coverage),
                      std::max(-180.0, center.longitude - half_coverage), 0.0),
            Geographic(std::min(85.05, center.latitude + half_coverage),
                      std::min(180.0, center.longitude + half_coverage), 0.0)
        );
    }

    // Ray-cast frustum corners to globe surface to find actual visible region
    // NDC corners: 8 corners of the frustum (4 near plane + 4 far plane)
    const glm::vec4 ndc_corners[8] = {
        glm::vec4(-1.0f, -1.0f, -1.0f, 1.0f),  // Near bottom-left
        glm::vec4( 1.0f, -1.0f, -1.0f, 1.0f),  // Near bottom-right
        glm::vec4( 1.0f,  1.0f, -1.0f, 1.0f),  // Near top-right
        glm::vec4(-1.0f,  1.0f, -1.0f, 1.0f),  // Near top-left
        glm::vec4(-1.0f, -1.0f,  1.0f, 1.0f),  // Far bottom-left
        glm::vec4( 1.0f, -1.0f,  1.0f, 1.0f),  // Far bottom-right
        glm::vec4( 1.0f,  1.0f,  1.0f, 1.0f),  // Far top-right
        glm::vec4(-1.0f,  1.0f,  1.0f, 1.0f)   // Far top-left
    };

    // Collect geographic coordinates of ray-sphere intersections
    double min_lat = 90.0;
    double max_lat = -90.0;
    double min_lon = 180.0;
    double max_lon = -180.0;
    int intersection_count = 0;

    for (int i = 0; i < 8; ++i) {
        // Unproject NDC corner to world space
        glm::vec4 world_homo = inv_vp * ndc_corners[i];
        if (std::abs(world_homo.w) < 1e-6f) {
            continue;  // Skip if homogeneous coordinate is near zero
        }
        glm::vec3 world_pos = glm::vec3(world_homo) / world_homo.w;

        // Ray from camera to this corner
        glm::vec3 ray_dir = world_pos - camera_pos;
        float ray_length = glm::length(ray_dir);
        if (ray_length < 1e-6f) {
            continue;  // Skip degenerate rays
        }
        ray_dir /= ray_length;  // Normalize

        // Ray-sphere intersection using quadratic formula
        // Ray: P = camera_pos + t * ray_dir
        // Sphere: |P|² = globe_radius²
        // Expand: |camera_pos + t * ray_dir|² = globe_radius²
        const float a = 1.0f;  // glm::dot(ray_dir, ray_dir) = 1.0 (normalized)
        const float b = 2.0f * glm::dot(camera_pos, ray_dir);
        const float c = glm::dot(camera_pos, camera_pos) - globe_radius * globe_radius;
        const float discriminant = b * b - 4.0f * a * c;

        if (discriminant >= 0.0f) {
            // Take the nearest intersection (smaller t, in front of camera)
            const float sqrt_discriminant = std::sqrt(discriminant);
            const float t1 = (-b - sqrt_discriminant) / (2.0f * a);
            const float t2 = (-b + sqrt_discriminant) / (2.0f * a);

            // Use the nearest positive t (intersection in front of camera)
            float t = (t1 > 0.0f) ? t1 : t2;
            if (t > 0.0f) {
                glm::vec3 intersection = camera_pos + t * ray_dir;
                Geographic geo = CartesianToGeographic(intersection);

                // Update bounds
                min_lat = std::min(min_lat, geo.latitude);
                max_lat = std::max(max_lat, geo.latitude);
                min_lon = std::min(min_lon, geo.longitude);
                max_lon = std::max(max_lon, geo.longitude);
                ++intersection_count;
            }
        }
    }

    // Fallback: if no valid intersections found, use old orbital method
    if (intersection_count == 0) {
        glm::vec3 view_point = glm::normalize(camera_pos) * globe_radius;
        Geographic center = CartesianToGeographic(view_point);

        float horizon_angle_rad = std::asin(std::min(1.0f, globe_radius / distance));
        float horizon_angle_deg = glm::degrees(horizon_angle_rad);
        float fov_y = 2.0f * std::atan(1.0f / proj_matrix[1][1]);
        float fov_deg = glm::degrees(fov_y);
        float coverage_angle = std::min(160.0f, std::max(horizon_angle_deg, fov_deg / 2.0f) * 2.0f);
        double half_coverage = static_cast<double>(coverage_angle) / 2.0;

        return GeographicBounds(
            Geographic(std::max(-85.05, center.latitude - half_coverage),
                      std::max(-180.0, center.longitude - half_coverage), 0.0),
            Geographic(std::min(85.05, center.latitude + half_coverage),
                      std::min(180.0, center.longitude + half_coverage), 0.0)
        );
    }

    // Add 10% margin for safety (avoid edge cases where tiles at boundaries are missed)
    const double lat_margin = (max_lat - min_lat) * 0.1;
    const double lon_margin = (max_lon - min_lon) * 0.1;

    min_lat = std::max(-85.05, min_lat - lat_margin);
    max_lat = std::min(85.05, max_lat + lat_margin);
    min_lon = std::max(-180.0, min_lon - lon_margin);
    max_lon = std::min(180.0, max_lon + lon_margin);

    // Handle date line wraparound (if bounds span more than 180° longitude)
    if (max_lon - min_lon > 180.0) {
        min_lon = -180.0;
        max_lon = 180.0;
    }

    return GeographicBounds(
        Geographic(min_lat, min_lon, 0.0),
        Geographic(max_lat, max_lon, 0.0)
    );
}

// ============================================================================
// Internal Helper Functions
// ============================================================================

glm::vec3 CoordinateMapper::GeographicToCartesian(const Geographic& geo, float radius) noexcept {
    // Convert degrees to radians
    double lat_rad = glm::radians(geo.latitude);
    double lon_rad = glm::radians(geo.longitude);

    // Spherical to Cartesian conversion
    // Convention: lon=0° → +Z, lon=90°E → +X, lat=90°N → +Y
    float x = radius * static_cast<float>(std::cos(lat_rad) * std::sin(lon_rad));
    float y = radius * static_cast<float>(std::sin(lat_rad));
    float z = radius * static_cast<float>(std::cos(lat_rad) * std::cos(lon_rad));

    return glm::vec3(x, y, z);
}

Geographic CoordinateMapper::CartesianToGeographic(const glm::vec3& cartesian) noexcept {
    // Normalize to get direction
    glm::vec3 dir = glm::normalize(cartesian);

    // Cartesian to spherical conversion
    // lat = asin(y)
    // lon = atan2(x, z)
    double lat = glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
    double lon = glm::degrees(std::atan2(dir.x, dir.z));

    return Geographic(lat, lon, 0.0);
}

bool CoordinateMapper::RaySphereIntersection(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_dir,
    const glm::vec3& sphere_center,
    float sphere_radius,
    glm::vec3& intersection_point) noexcept {

    // Ray-sphere intersection using quadratic formula
    // Ray: P(t) = origin + t * direction
    // Sphere: |P - center|² = radius²
    //
    // Substituting ray into sphere equation:
    // |origin + t*dir - center|² = radius²
    //
    // Let oc = origin - center:
    // a = dot(dir, dir) = 1 (if dir is normalized)
    // b = 2 * dot(oc, dir)
    // c = dot(oc, oc) - radius²
    //
    // t = (-b ± sqrt(b² - 4ac)) / 2a

    glm::vec3 oc = ray_origin - sphere_center;

    float a = glm::dot(ray_dir, ray_dir);
    float b = 2.0f * glm::dot(oc, ray_dir);
    float c = glm::dot(oc, oc) - sphere_radius * sphere_radius;

    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f) {
        return false;  // No intersection
    }

    // Two possible intersection points
    float sqrt_discriminant = std::sqrt(discriminant);
    float t1 = (-b - sqrt_discriminant) / (2.0f * a);
    float t2 = (-b + sqrt_discriminant) / (2.0f * a);

    // Choose the appropriate intersection point:
    // - If camera is outside sphere: use the closer positive t (entry point)
    // - If camera is inside sphere: use the farther positive t (exit point)
    // - For globe viewing, we typically want the front-facing intersection

    float t;
    if (t1 > 0.0f && t2 > 0.0f) {
        // Both intersections in front of camera - use closer one (front face)
        t = t1;
    } else if (t1 > 0.0f) {
        // Only t1 is positive
        t = t1;
    } else if (t2 > 0.0f) {
        // Only t2 is positive (camera might be inside sphere)
        t = t2;
    } else {
        // Both behind camera
        return false;
    }

    intersection_point = ray_origin + t * ray_dir;
    return true;
}

// ============================================================================
// Spherical Tile Mapping (for 3D Globe)
// ============================================================================

TileCoordinates CoordinateMapper::GeographicToSphericalTile(
    const Geographic& geo,
    int32_t zoom) noexcept {

    // For 3D sphere rendering with Web Mercator tiles:
    // - Vertex positions use simple sphere math (no distortion)
    // - Tile coordinates use Web Mercator (to match XYZ tile server layout)
    // This function maps 3D vertex positions to Web Mercator tile coordinates

    const int32_t n = 1 << zoom;  // 2^zoom

    // Longitude: simple linear mapping
    const double norm_lon = (geo.longitude + 180.0) / 360.0;

    // Latitude: Web Mercator projection (to match tile server)
    const double lat_clamped = std::clamp(geo.latitude, -85.0511, 85.0511);
    const double lat_clamped_rad = lat_clamped * M_PI / 180.0;

    // Web Mercator Y: y = ln(tan(π/4 + lat/2))
    const double merc_y = std::log(std::tan(M_PI / 4.0 + lat_clamped_rad / 2.0));
    // Normalize to [0, 1]: y ∈ [-π, π] → [0, 1]
    const double norm_lat = (1.0 - merc_y / M_PI) / 2.0;

    // Convert to tile coordinates
    const int32_t tile_x = static_cast<int32_t>(std::floor(norm_lon * n));
    const int32_t tile_y = static_cast<int32_t>(std::floor(norm_lat * n));

    // Clamp to valid range [0, n-1]
    const int32_t clamped_x = std::clamp(tile_x, 0, n - 1);
    const int32_t clamped_y = std::clamp(tile_y, 0, n - 1);

    return TileCoordinates{clamped_x, clamped_y, zoom};
}

glm::vec2 CoordinateMapper::GetTileFraction(
    const Geographic& geo,
    const TileCoordinates& tile) noexcept {

    const int32_t n = 1 << tile.zoom;  // 2^zoom

    // Longitude: simple linear mapping
    const double norm_lon = (geo.longitude + 180.0) / 360.0;

    // Latitude: Web Mercator projection (to match tile server)
    const double lat_clamped = std::clamp(geo.latitude, -85.0511, 85.0511);
    const double lat_clamped_rad = lat_clamped * M_PI / 180.0;
    const double merc_y = std::log(std::tan(M_PI / 4.0 + lat_clamped_rad / 2.0));
    const double norm_lat = (1.0 - merc_y / M_PI) / 2.0;

    // Calculate tile-space coordinates (continuous)
    const double tile_lon = norm_lon * n;
    const double tile_lat = norm_lat * n;

    // Calculate fractional position within tile
    const double frac_x = tile_lon - tile.x;
    const double frac_y = tile_lat - tile.y;

    // Clamp to [0, 1] and convert to float
    return glm::vec2(
        static_cast<float>(std::clamp(frac_x, 0.0, 1.0)),
        static_cast<float>(std::clamp(frac_y, 0.0, 1.0))
    );
}

} // namespace coordinates
} // namespace earth_map
