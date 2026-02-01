/**
 * @file coordinate_spaces.h
 * @brief Type-safe coordinate system definitions for GIS operations
 *
 * This file defines explicit coordinate space types to prevent mixing
 * coordinates from different systems (geographic, world, screen, projected, tile).
 *
 * Design Principle: Each coordinate space has its own strong type to ensure
 * compile-time safety and prevent coordinate system confusion bugs.
 *
 * @see COORDINATE_SYSTEM_ARCHITECTURE_PLAN.md for full architecture design
 */

#pragma once

#include <glm/glm.hpp>
#include <array>
#include <optional>
#include <cmath>
#include <limits>

namespace earth_map {
namespace coordinates {

// ============================================================================
// GEOGRAPHIC SPACE (WGS84 - Degrees)
// ============================================================================

/**
 * @brief Geographic coordinates in WGS84 datum (latitude, longitude, altitude)
 * @note This is the primary coordinate system for user-facing APIs
 *
 * Constraints:
 * - Latitude: [-90°, +90°] (South to North)
 * - Longitude: [-180°, +180°] (West to East)
 * - Altitude: meters (reference defined by altitude_reference field)
 *
 * **Altitude Reference**: The altitude field's meaning depends on altitude_reference:
 * - WGS84_ELLIPSOID (default): Height above WGS84 ellipsoid (standard for SRTM, GPS)
 * - MEAN_SEA_LEVEL: Height above geoid (orthometric height)
 * - TERRAIN: Height above local terrain (AGL - Above Ground Level)
 * - ABSOLUTE: Distance from Earth's center (geocentric radius)
 *
 * @see altitude_reference.h for AltitudeReference enum and conversion utilities
 */
struct Geographic {
    double latitude;   ///< Latitude in degrees [-90, 90]
    double longitude;  ///< Longitude in degrees [-180, 180]
    double altitude;   ///< Altitude in meters (reference defined by altitude_reference)

    // Note: altitude_reference not included in constexpr constructor to maintain C++17 compatibility
    // It will be default-initialized to WGS84_ELLIPSOID (value 0)

    /**
     * @brief Default constructor - creates invalid coordinates
     */
    constexpr Geographic()
        : latitude(std::numeric_limits<double>::quiet_NaN())
        , longitude(std::numeric_limits<double>::quiet_NaN())
        , altitude(0.0) {}

    /**
     * @brief Construct from lat/lon/alt
     * @param lat Latitude in degrees
     * @param lon Longitude in degrees
     * @param alt Altitude in meters (default: 0.0, referenced to WGS84 ellipsoid)
     */
    constexpr Geographic(double lat, double lon, double alt = 0.0)
        : latitude(lat), longitude(lon), altitude(alt) {}

    /**
     * @brief Check if coordinates are within valid WGS84 bounds
     * @return true if valid, false otherwise
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return latitude >= -90.0 && latitude <= 90.0 &&
               longitude >= -180.0 && longitude <= 180.0 &&
               !std::isnan(latitude) && !std::isnan(longitude);
    }

    /**
     * @brief Normalize longitude to [-180, 180] range
     * @return Normalized geographic coordinates
     */
    [[nodiscard]] Geographic Normalized() const noexcept;

    /**
     * @brief Check if coordinates are approximately equal
     * @param other Other coordinates to compare
     * @param epsilon Tolerance for comparison (default: 1e-9 degrees)
     * @return true if approximately equal
     */
    [[nodiscard]] bool IsApproximatelyEqual(const Geographic& other,
                                            double epsilon = 1e-9) const noexcept;

    /**
     * @brief Equality operator
     */
    constexpr bool operator==(const Geographic& other) const noexcept {
        return latitude == other.latitude &&
               longitude == other.longitude &&
               altitude == other.altitude;
    }

    constexpr bool operator!=(const Geographic& other) const noexcept {
        return !(*this == other);
    }
};

/**
 * @brief Axis-aligned bounding box in geographic space
 * @note min = southwest corner, max = northeast corner
 */
struct GeographicBounds {
    Geographic min;  ///< Southwest corner (min lat, min lon)
    Geographic max;  ///< Northeast corner (max lat, max lon)

    /**
     * @brief Default constructor - creates invalid bounds
     */
    constexpr GeographicBounds()
        : min(90.0, 180.0, 0.0)    // Inverted bounds
        , max(-90.0, -180.0, 0.0) {}

    /**
     * @brief Construct from min/max corners
     */
    constexpr GeographicBounds(const Geographic& min_corner, const Geographic& max_corner)
        : min(min_corner), max(max_corner) {}

    /**
     * @brief Check if bounds are valid
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return min.latitude <= max.latitude &&
               min.longitude <= max.longitude &&
               min.IsValid() && max.IsValid();
    }

    /**
     * @brief Check if point is contained within bounds
     * @param point Geographic point to test
     * @return true if point is inside bounds
     */
    [[nodiscard]] bool Contains(const Geographic& point) const noexcept;

    /**
     * @brief Get center point of bounding box
     */
    [[nodiscard]] Geographic GetCenter() const noexcept;

    /**
     * @brief Get width (longitude extent) in degrees
     */
    [[nodiscard]] constexpr double Width() const noexcept {
        return max.longitude - min.longitude;
    }

    /**
     * @brief Get height (latitude extent) in degrees
     */
    [[nodiscard]] constexpr double Height() const noexcept {
        return max.latitude - min.latitude;
    }

    /**
     * @brief Expand bounds to include a point
     */
    void ExpandToInclude(const Geographic& point) noexcept;

    /**
     * @brief Check if this bounds intersects another
     */
    [[nodiscard]] bool Intersects(const GeographicBounds& other) const noexcept;
};

// ============================================================================
// SCREEN SPACE (Pixels)
// ============================================================================

/**
 * @brief Screen coordinates in pixels (origin: top-left)
 * @note Used for user interaction - mouse clicks, UI elements
 *
 * Convention:
 * - Origin: top-left corner of screen
 * - X-axis: positive to the right
 * - Y-axis: positive downward
 */
struct Screen {
    double x;  ///< Horizontal pixel coordinate from left edge
    double y;  ///< Vertical pixel coordinate from top edge

    /**
     * @brief Default constructor
     */
    constexpr Screen() : x(0.0), y(0.0) {}

    /**
     * @brief Construct from x,y pixel coordinates
     */
    constexpr Screen(double x_, double y_) : x(x_), y(y_) {}

    /**
     * @brief Check if coordinates are valid (non-negative)
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return x >= 0.0 && y >= 0.0 &&
               !std::isnan(x) && !std::isnan(y);
    }

    constexpr bool operator==(const Screen& other) const noexcept {
        return x == other.x && y == other.y;
    }

    constexpr bool operator!=(const Screen& other) const noexcept {
        return !(*this == other);
    }
};

/**
 * @brief Rectangular region in screen space
 */
struct ScreenBounds {
    Screen min;  ///< Top-left corner
    Screen max;  ///< Bottom-right corner

    constexpr ScreenBounds() : min(), max() {}
    constexpr ScreenBounds(const Screen& min_, const Screen& max_)
        : min(min_), max(max_) {}

    [[nodiscard]] constexpr double Width() const noexcept {
        return max.x - min.x;
    }

    [[nodiscard]] constexpr double Height() const noexcept {
        return max.y - min.y;
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return min.x <= max.x && min.y <= max.y;
    }

    [[nodiscard]] bool Contains(const Screen& point) const noexcept {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y;
    }
};

// ============================================================================
// PROJECTED SPACE (Web Mercator - Meters)
// ============================================================================

/**
 * @brief Projected coordinates in Web Mercator (EPSG:3857)
 * @note Internal coordinate system for tile mathematics
 *
 * Constraints:
 * - Range: [-20037508.34, +20037508.34] meters in both X and Y
 * - Origin: Intersection of equator and prime meridian
 * - Units: meters
 */
struct Projected {
    double x;  ///< Easting in meters
    double y;  ///< Northing in meters

    constexpr Projected() : x(0.0), y(0.0) {}
    constexpr Projected(double x_, double y_) : x(x_), y(y_) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        constexpr double HALF_WORLD = 20037508.342789244;
        return x >= -HALF_WORLD && x <= HALF_WORLD &&
               y >= -HALF_WORLD && y <= HALF_WORLD &&
               !std::isnan(x) && !std::isnan(y);
    }

    constexpr bool operator==(const Projected& other) const noexcept {
        return x == other.x && y == other.y;
    }

    constexpr bool operator!=(const Projected& other) const noexcept {
        return !(*this == other);
    }
};

/**
 * @brief Bounding box in projected space
 */
struct ProjectedBounds {
    Projected min;  ///< Minimum corner (southwest)
    Projected max;  ///< Maximum corner (northeast)

    constexpr ProjectedBounds() : min(), max() {}
    constexpr ProjectedBounds(const Projected& min_, const Projected& max_)
        : min(min_), max(max_) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return min.x <= max.x && min.y <= max.y &&
               min.IsValid() && max.IsValid();
    }

    [[nodiscard]] bool Contains(const Projected& point) const noexcept {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y;
    }
};

// ============================================================================
// WORLD SPACE (OpenGL Rendering Units)
// ============================================================================

/**
 * @brief 3D position in OpenGL world space
 * @note Internal coordinate system for rendering - users never see this
 *
 * Convention:
 * - Globe centered at origin (0, 0, 0)
 * - Globe radius: 1.0 units
 * - Y-axis: North pole (+Y)
 * - Z-axis: Prime meridian (lon=0°)
 * - X-axis: 90° East longitude
 */
struct World {
    glm::vec3 position;  ///< 3D position in world space

    constexpr World() : position(0.0f, 0.0f, 0.0f) {}
    constexpr World(float x, float y, float z) : position(x, y, z) {}
    constexpr World(const glm::vec3& pos) : position(pos) {}

    /**
     * @brief Get distance from origin
     */
    [[nodiscard]] float Distance() const noexcept {
        return glm::length(position);
    }

    /**
     * @brief Get normalized direction vector
     */
    [[nodiscard]] glm::vec3 Direction() const noexcept {
        float len = Distance();
        return len > 0.0f ? position / len : glm::vec3(0.0f, 0.0f, 1.0f);
    }

    /**
     * @brief Check if position is approximately on globe surface (radius ≈ 1.0)
     */
    [[nodiscard]] bool IsOnGlobeSurface(float epsilon = 0.01f) const noexcept {
        float dist = Distance();
        return std::abs(dist - 1.0f) < epsilon;
    }

    constexpr bool operator==(const World& other) const noexcept {
        return position == other.position;
    }

    constexpr bool operator!=(const World& other) const noexcept {
        return !(*this == other);
    }
};

/**
 * @brief View frustum in world space (6 planes)
 */
struct WorldFrustum {
    std::array<glm::vec4, 6> planes;  ///< Frustum planes (left, right, bottom, top, near, far)

    WorldFrustum() : planes{} {}

    /**
     * @brief Check if point is inside frustum
     */
    [[nodiscard]] bool Contains(const World& point) const noexcept;

    /**
     * @brief Check if sphere intersects frustum
     * @param center Sphere center in world space
     * @param radius Sphere radius
     */
    [[nodiscard]] bool Intersects(const World& center, float radius) const noexcept;

    /**
     * @brief Extract frustum from view-projection matrix
     * @param view_proj Combined view-projection matrix
     */
    static WorldFrustum FromMatrix(const glm::mat4& view_proj) noexcept;
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Calculate great circle distance between two geographic points
 * @param from Start point
 * @param to End point
 * @return Distance in meters using Haversine formula
 */
[[nodiscard]] double CalculateGreatCircleDistance(const Geographic& from,
                                                   const Geographic& to) noexcept;

/**
 * @brief Calculate bearing from one point to another
 * @param from Start point
 * @param to End point
 * @return Bearing in degrees [0, 360), where 0° is North
 */
[[nodiscard]] double CalculateBearing(const Geographic& from,
                                       const Geographic& to) noexcept;

/**
 * @brief Calculate destination point given start, bearing, and distance
 * @param start Starting point
 * @param bearing_degrees Bearing in degrees (0° = North)
 * @param distance_meters Distance to travel in meters
 * @return Destination geographic coordinates
 */
[[nodiscard]] Geographic CalculateDestination(const Geographic& start,
                                               double bearing_degrees,
                                               double distance_meters) noexcept;

} // namespace coordinates
} // namespace earth_map
