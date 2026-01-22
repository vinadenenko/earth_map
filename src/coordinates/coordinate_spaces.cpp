/**
 * @file coordinate_spaces.cpp
 * @brief Implementation of type-safe coordinate space operations
 */

#include "../../include/earth_map/coordinates/coordinate_spaces.h"
#include <cmath>
#include <algorithm>

namespace earth_map {
namespace coordinates {

// ============================================================================
// Geographic Implementation
// ============================================================================

Geographic Geographic::Normalized() const noexcept {
    // Normalize longitude to [-180, 180]
    double norm_lon = longitude;

    // Handle invalid input - just wrap longitude
    if (std::isnan(longitude) || std::isnan(latitude)) {
        return *this;
    }

    while (norm_lon > 180.0) norm_lon -= 360.0;
    while (norm_lon < -180.0) norm_lon += 360.0;

    // Clamp latitude to valid range [-90, 90]
    double norm_lat = std::max(-90.0, std::min(90.0, latitude));

    return Geographic(norm_lat, norm_lon, altitude);
}

bool Geographic::IsApproximatelyEqual(const Geographic& other, double epsilon) const noexcept {
    return std::abs(latitude - other.latitude) < epsilon &&
           std::abs(longitude - other.longitude) < epsilon &&
           std::abs(altitude - other.altitude) < epsilon;
}

// ============================================================================
// GeographicBounds Implementation
// ============================================================================

bool GeographicBounds::Contains(const Geographic& point) const noexcept {
    if (!IsValid() || !point.IsValid()) {
        return false;
    }

    return point.latitude >= min.latitude && point.latitude <= max.latitude &&
           point.longitude >= min.longitude && point.longitude <= max.longitude;
}

Geographic GeographicBounds::GetCenter() const noexcept {
    if (!IsValid()) {
        return Geographic();
    }

    return Geographic(
        (min.latitude + max.latitude) / 2.0,
        (min.longitude + max.longitude) / 2.0,
        (min.altitude + max.altitude) / 2.0
    );
}

void GeographicBounds::ExpandToInclude(const Geographic& point) noexcept {
    if (!point.IsValid()) {
        return;
    }

    if (!IsValid()) {
        // First point - initialize bounds
        min = point;
        max = point;
        return;
    }

    min.latitude = std::min(min.latitude, point.latitude);
    min.longitude = std::min(min.longitude, point.longitude);
    min.altitude = std::min(min.altitude, point.altitude);

    max.latitude = std::max(max.latitude, point.latitude);
    max.longitude = std::max(max.longitude, point.longitude);
    max.altitude = std::max(max.altitude, point.altitude);
}

bool GeographicBounds::Intersects(const GeographicBounds& other) const noexcept {
    if (!IsValid() || !other.IsValid()) {
        return false;
    }

    // Check for no overlap (separating axis theorem)
    if (max.latitude < other.min.latitude || min.latitude > other.max.latitude) {
        return false;
    }

    if (max.longitude < other.min.longitude || min.longitude > other.max.longitude) {
        return false;
    }

    return true;
}

// ============================================================================
// WorldFrustum Implementation
// ============================================================================

bool WorldFrustum::Contains(const World& point) const noexcept {
    // Test point against all 6 frustum planes
    for (const auto& plane : planes) {
        // Plane equation: Ax + By + Cz + D = 0
        // Point is inside if (dot(normal, point) + D) >= 0
        float distance = glm::dot(glm::vec3(plane), point.position) + plane.w;
        if (distance < 0.0f) {
            return false;  // Point is outside this plane
        }
    }
    return true;
}

bool WorldFrustum::Intersects(const World& center, float radius) const noexcept {
    // Test sphere against all 6 frustum planes
    for (const auto& plane : planes) {
        float distance = glm::dot(glm::vec3(plane), center.position) + plane.w;
        if (distance < -radius) {
            return false;  // Sphere is completely outside this plane
        }
    }
    return true;
}

WorldFrustum WorldFrustum::FromMatrix(const glm::mat4& view_proj) noexcept {
    WorldFrustum frustum;

    // Extract frustum planes from view-projection matrix
    // See: http://www.cs.otago.ac.nz/postgrads/alexis/planeExtraction.pdf

    const glm::mat4& m = view_proj;

    // Left plane
    frustum.planes[0] = glm::vec4(
        m[0][3] + m[0][0],
        m[1][3] + m[1][0],
        m[2][3] + m[2][0],
        m[3][3] + m[3][0]
    );

    // Right plane
    frustum.planes[1] = glm::vec4(
        m[0][3] - m[0][0],
        m[1][3] - m[1][0],
        m[2][3] - m[2][0],
        m[3][3] - m[3][0]
    );

    // Bottom plane
    frustum.planes[2] = glm::vec4(
        m[0][3] + m[0][1],
        m[1][3] + m[1][1],
        m[2][3] + m[2][1],
        m[3][3] + m[3][1]
    );

    // Top plane
    frustum.planes[3] = glm::vec4(
        m[0][3] - m[0][1],
        m[1][3] - m[1][1],
        m[2][3] - m[2][1],
        m[3][3] - m[3][1]
    );

    // Near plane
    frustum.planes[4] = glm::vec4(
        m[0][3] + m[0][2],
        m[1][3] + m[1][2],
        m[2][3] + m[2][2],
        m[3][3] + m[3][2]
    );

    // Far plane
    frustum.planes[5] = glm::vec4(
        m[0][3] - m[0][2],
        m[1][3] - m[1][2],
        m[2][3] - m[2][2],
        m[3][3] - m[3][2]
    );

    // Normalize planes
    for (auto& plane : frustum.planes) {
        float length = glm::length(glm::vec3(plane));
        if (length > 0.0f) {
            plane /= length;
        }
    }

    return frustum;
}

// ============================================================================
// Utility Functions Implementation
// ============================================================================

double CalculateGreatCircleDistance(const Geographic& from, const Geographic& to) noexcept {
    if (!from.IsValid() || !to.IsValid()) {
        return 0.0;
    }

    // Haversine formula
    // a = sin²(Δlat/2) + cos(lat1) · cos(lat2) · sin²(Δlon/2)
    // c = 2 · atan2(√a, √(1−a))
    // d = R · c

    constexpr double EARTH_RADIUS_METERS = 6371000.0;  // WGS84 mean radius

    const double lat1 = glm::radians(from.latitude);
    const double lat2 = glm::radians(to.latitude);
    const double delta_lat = glm::radians(to.latitude - from.latitude);
    const double delta_lon = glm::radians(to.longitude - from.longitude);

    const double a = std::sin(delta_lat / 2.0) * std::sin(delta_lat / 2.0) +
                     std::cos(lat1) * std::cos(lat2) *
                     std::sin(delta_lon / 2.0) * std::sin(delta_lon / 2.0);

    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    return EARTH_RADIUS_METERS * c;
}

double CalculateBearing(const Geographic& from, const Geographic& to) noexcept {
    if (!from.IsValid() || !to.IsValid()) {
        return 0.0;
    }

    // Forward azimuth formula
    // θ = atan2(sin(Δlon)·cos(lat2), cos(lat1)·sin(lat2) − sin(lat1)·cos(lat2)·cos(Δlon))

    const double lat1 = glm::radians(from.latitude);
    const double lat2 = glm::radians(to.latitude);
    const double delta_lon = glm::radians(to.longitude - from.longitude);

    const double y = std::sin(delta_lon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) -
                     std::sin(lat1) * std::cos(lat2) * std::cos(delta_lon);

    double bearing = std::atan2(y, x);
    bearing = glm::degrees(bearing);

    // Normalize to [0, 360)
    bearing = std::fmod(bearing + 360.0, 360.0);

    return bearing;
}

Geographic CalculateDestination(const Geographic& start,
                                double bearing_degrees,
                                double distance_meters) noexcept {
    if (!start.IsValid() || distance_meters < 0.0) {
        return Geographic();
    }

    // Forward calculation using great circle formulas
    // lat2 = asin(sin(lat1)·cos(δ) + cos(lat1)·sin(δ)·cos(θ))
    // lon2 = lon1 + atan2(sin(θ)·sin(δ)·cos(lat1), cos(δ) − sin(lat1)·sin(lat2))
    // where δ = d/R (angular distance)

    constexpr double EARTH_RADIUS_METERS = 6371000.0;

    const double lat1 = glm::radians(start.latitude);
    const double lon1 = glm::radians(start.longitude);
    const double bearing = glm::radians(bearing_degrees);
    const double angular_distance = distance_meters / EARTH_RADIUS_METERS;

    const double lat2 = std::asin(
        std::sin(lat1) * std::cos(angular_distance) +
        std::cos(lat1) * std::sin(angular_distance) * std::cos(bearing)
    );

    const double lon2 = lon1 + std::atan2(
        std::sin(bearing) * std::sin(angular_distance) * std::cos(lat1),
        std::cos(angular_distance) - std::sin(lat1) * std::sin(lat2)
    );

    return Geographic(
        glm::degrees(lat2),
        glm::degrees(lon2),
        start.altitude
    );
}

} // namespace coordinates
} // namespace earth_map
