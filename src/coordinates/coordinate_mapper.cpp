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

// ============================================================================
// Utility: Bounds Conversions
// ============================================================================

GeographicBounds CoordinateMapper::CalculateVisibleGeographicBounds(
    const World& camera_world,
    const glm::mat4& /*view_matrix*/,  // Currently unused - for future frustum-based calculation
    const glm::mat4& proj_matrix,
    float globe_radius) noexcept {

    // Calculate look direction (where camera is pointing)
    glm::vec3 camera_pos = camera_world.position;
    float distance = glm::length(camera_pos);

    // For orbital camera: camera looks toward origin
    // The point we're viewing is on the near side of the sphere, in the direction of the camera from origin
    glm::vec3 view_point = glm::normalize(camera_pos) * globe_radius;

    // Convert to geographic coordinates
    Geographic center = CartesianToGeographic(view_point);

    // Calculate visible angular extent based on distance and FOV
    // Horizon angle: angle from camera to tangent of sphere
    float horizon_angle_rad = std::asin(globe_radius / distance);
    float horizon_angle_deg = glm::degrees(horizon_angle_rad);

    // Extract FOV from projection matrix (rough estimate)
    // For perspective projection: fov = 2 * atan(1 / proj[1][1])
    float fov_y = 2.0f * std::atan(1.0f / proj_matrix[1][1]);
    float fov_deg = glm::degrees(fov_y);

    // Use larger of horizon angle or FOV
    float visible_angle = std::max(horizon_angle_deg, fov_deg / 2.0f);

    // Add generous margin for typical viewing distances
    float coverage_angle = visible_angle * 2.0f;  // 100% margin

    // Clamp to reasonable bounds
    coverage_angle = std::min(160.0f, coverage_angle);

    // Calculate bounds
    double half_coverage = static_cast<double>(coverage_angle) / 2.0;

    double min_lat = std::max(-85.05, center.latitude - half_coverage);
    double max_lat = std::min(85.05, center.latitude + half_coverage);
    double min_lon = center.longitude - half_coverage;
    double max_lon = center.longitude + half_coverage;

    // Handle date line wraparound
    if (min_lon < -180.0 || max_lon > 180.0) {
        min_lon = -180.0;
        max_lon = 180.0;
    } else {
        min_lon = std::max(-180.0, min_lon);
        max_lon = std::min(180.0, max_lon);
    }

    Geographic min_corner(min_lat, min_lon, 0.0);
    Geographic max_corner(max_lat, max_lon, 0.0);

    return GeographicBounds(min_corner, max_corner);
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

} // namespace coordinates
} // namespace earth_map
