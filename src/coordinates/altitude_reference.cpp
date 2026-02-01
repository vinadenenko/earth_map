/**
 * @file altitude_reference.cpp
 * @brief Implementation of altitude reference system conversions
 */

#include "../../include/earth_map/coordinates/altitude_reference.h"
#include "../../include/earth_map/coordinates/coordinate_spaces.h"
#include <cmath>
#include <glm/glm.hpp>

namespace earth_map {
namespace coordinates {

// ============================================================================
// Altitude Conversion
// ============================================================================

double AltitudeConverter::Convert(
    double altitude,
    AltitudeReference from,
    AltitudeReference to,
    const Geographic& location,
    double terrain_elevation) noexcept {

    // Same reference - no conversion needed
    if (from == to) {
        return altitude;
    }

    // Convert to WGS84 ellipsoid as intermediate reference
    double wgs84_height = altitude;

    // From source reference to WGS84 ellipsoid
    switch (from) {
        case AltitudeReference::WGS84_ELLIPSOID:
            // Already in WGS84
            wgs84_height = altitude;
            break;

        case AltitudeReference::MEAN_SEA_LEVEL: {
            // MSL to WGS84: add geoid height
            // Height above ellipsoid = Height above geoid + Geoid height
            double geoid_height = GetGeoidHeight(location);
            wgs84_height = altitude + geoid_height;
            break;
        }

        case AltitudeReference::TERRAIN:
            // Terrain-relative to WGS84: add terrain elevation
            wgs84_height = altitude + terrain_elevation;
            break;

        case AltitudeReference::ABSOLUTE: {
            // Absolute (geocentric) to WGS84: subtract ellipsoid radius
            double ellipsoid_radius = GetEllipsoidRadius(location.latitude);
            wgs84_height = altitude - ellipsoid_radius;
            break;
        }
    }

    // From WGS84 ellipsoid to target reference
    switch (to) {
        case AltitudeReference::WGS84_ELLIPSOID:
            return wgs84_height;

        case AltitudeReference::MEAN_SEA_LEVEL: {
            // WGS84 to MSL: subtract geoid height
            double geoid_height = GetGeoidHeight(location);
            return wgs84_height - geoid_height;
        }

        case AltitudeReference::TERRAIN:
            // WGS84 to terrain-relative: subtract terrain elevation
            return wgs84_height - terrain_elevation;

        case AltitudeReference::ABSOLUTE: {
            // WGS84 to absolute: add ellipsoid radius
            double ellipsoid_radius = GetEllipsoidRadius(location.latitude);
            return wgs84_height + ellipsoid_radius;
        }
    }

    // Should never reach here
    return altitude;
}

double AltitudeConverter::GetGeoidHeight(const Geographic& location) noexcept {
    // TODO: Implement full geoid model (EGM96 or EGM2008)
    // For now, return approximate geoid height based on simple model

    // Very rough approximation: geoid height varies with latitude
    // Real geoid heights range from -110m to +85m globally
    // This is a simplified model for initial implementation

    double lat_rad = glm::radians(location.latitude);

    // Simple approximation: geoid is below ellipsoid near poles, above at equator
    // This is NOT accurate and should be replaced with EGM96/EGM2008 data
    double approx_geoid_height = 0.0;  // Simplified: assume geoid ≈ ellipsoid

    // For better accuracy, we would:
    // 1. Load EGM96 or EGM2008 geoid grid data
    // 2. Interpolate geoid height at (lat, lon)
    // 3. Return interpolated value

    // Placeholder simple model (very rough):
    // Geoid is slightly higher at equator, lower at poles
    approx_geoid_height = -20.0 * std::cos(2.0 * lat_rad);  // Range: -20m to +20m

    return approx_geoid_height;
}

double AltitudeConverter::GetEllipsoidRadius(double latitude_degrees) noexcept {
    double lat_rad = glm::radians(latitude_degrees);
    double sin_lat = std::sin(lat_rad);
    double cos_lat = std::cos(lat_rad);

    // WGS84 ellipsoid radius at given latitude
    // Formula: R = sqrt((a²cos(φ))² + (b²sin(φ))²) / sqrt((a*cos(φ))² + (b*sin(φ))²)
    // where a = semi-major axis, b = semi-minor axis, φ = latitude

    double a = WGS84_SEMI_MAJOR_AXIS;
    double b = WGS84_SEMI_MINOR_AXIS;

    double a_cos = a * cos_lat;
    double b_sin = b * sin_lat;

    double numerator = (a * a * cos_lat) * (a * a * cos_lat) +
                       (b * b * sin_lat) * (b * b * sin_lat);
    double denominator = a_cos * a_cos + b_sin * b_sin;

    return std::sqrt(numerator / denominator);
}

double AltitudeConverter::GetGeocentricRadius(const Geographic& location) noexcept {
    double ellipsoid_radius = GetEllipsoidRadius(location.latitude);
    return ellipsoid_radius + location.altitude;
}

} // namespace coordinates
} // namespace earth_map
