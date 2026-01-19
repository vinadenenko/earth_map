/**
 * @file coordinate_system.cpp
 * @brief Coordinate system transformations implementation
 */

#include "../../include/earth_map/math/coordinate_system.h"
#include <cmath>
#include <stdexcept>

namespace earth_map {

glm::dvec3 CoordinateSystem::GeographicToECEF(const GeographicCoordinates& geo) {
    if (!CoordinateValidator::IsValidGeographic(geo)) {
        throw std::invalid_argument("Invalid geographic coordinates");
    }
    
    const double lat_rad = geo.LatitudeRadians();
    const double lon_rad = geo.LongitudeRadians();
    
    const double sin_lat = std::sin(lat_rad);
    const double cos_lat = std::cos(lat_rad);
    const double sin_lon = std::sin(lon_rad);
    const double cos_lon = std::cos(lon_rad);
    
    const double a = WGS84Ellipsoid::SEMI_MAJOR_AXIS;
    const double e2 = WGS84Ellipsoid::ECCENTRICITY_SQUARED;
    
    // Radius of curvature in the prime vertical
    const double N = a / std::sqrt(1.0 - e2 * sin_lat * sin_lat);
    
    // ECEF coordinates
    const double x = (N + geo.altitude) * cos_lat * cos_lon;
    const double y = (N + geo.altitude) * cos_lat * sin_lon;
    const double z = (N * (1.0 - e2) + geo.altitude) * sin_lat;
    
    return glm::dvec3(x, y, z);
}

GeographicCoordinates CoordinateSystem::ECEFToGeographic(const glm::dvec3& ecef) {
    if (!CoordinateValidator::IsValidECEF(ecef)) {
        throw std::invalid_argument("Invalid ECEF coordinates");
    }
    
    const double a = WGS84Ellipsoid::SEMI_MAJOR_AXIS;
    const double b = WGS84Ellipsoid::SEMI_MINOR_AXIS;
    const double e2 = WGS84Ellipsoid::ECCENTRICITY_SQUARED;
    const double e_prime_sq = WGS84Ellipsoid::SECOND_ECCENTRICITY_SQUARED;
    
    const double x = ecef.x;
    const double y = ecef.y;
    const double z = ecef.z;
    
    // Longitude calculation
    const double longitude = std::atan2(y, x);
    
    // Latitude calculation (iterative method)
    const double p = std::sqrt(x * x + y * y);
    const double theta = std::atan2(z * a, p * b);
    
    double latitude = std::atan2(z + e_prime_sq * b * std::pow(std::sin(theta), 3),
                                 p - e2 * a * std::pow(std::cos(theta), 3));
    
    // Altitude calculation
    const double sin_lat = std::sin(latitude);
    const double N = a / std::sqrt(1.0 - e2 * sin_lat * sin_lat);
    const double altitude = p / std::cos(latitude) - N;
    
    return GeographicCoordinates(
        RadiansToDegrees(latitude),
        RadiansToDegrees(longitude),
        altitude
    );
}

glm::dvec3 CoordinateSystem::GeographicToENU(const GeographicCoordinates& geo, 
                                             const GeographicCoordinates& origin) {
    // Convert both points to ECEF
    const glm::dvec3 geo_ecef = GeographicToECEF(geo);
    const glm::dvec3 origin_ecef = GeographicToECEF(origin);
    
    // Get ENU to ECEF transformation matrix
    const glm::dmat4 enu_to_ecef = ENUToECEFMatrix(origin);
    
    // Transform ECEF difference to ENU
    const glm::dvec3 diff = geo_ecef - origin_ecef;
    const glm::dvec4 enu_homogeneous = glm::inverse(enu_to_ecef) * glm::dvec4(diff, 0.0);
    
    return glm::dvec3(enu_homogeneous.x, enu_homogeneous.y, enu_homogeneous.z);
}

GeographicCoordinates CoordinateSystem::ENUToGeographic(const glm::dvec3& enu, 
                                                         const GeographicCoordinates& origin) {
    // Get ENU to ECEF transformation matrix
    const glm::dmat4 enu_to_ecef = ENUToECEFMatrix(origin);
    
    // Transform ENU to ECEF
    const glm::dvec4 enu_homogeneous(enu.x, enu.y, enu.z, 0.0);
    const glm::dvec4 ecef_diff = enu_to_ecef * enu_homogeneous;
    
    const glm::dvec3 origin_ecef = GeographicToECEF(origin);
    const glm::dvec3 geo_ecef = origin_ecef + glm::dvec3(ecef_diff.x, ecef_diff.y, ecef_diff.z);
    
    return ECEFToGeographic(geo_ecef);
}

glm::dvec3 CoordinateSystem::SurfaceNormal(const GeographicCoordinates& geo) {
    const double lat_rad = geo.LatitudeRadians();
    const double lon_rad = geo.LongitudeRadians();
    
    const double cos_lat = std::cos(lat_rad);
    const double sin_lat = std::sin(lat_rad);
    const double cos_lon = std::cos(lon_rad);
    const double sin_lon = std::sin(lon_rad);
    
    // Normal at ellipsoid surface
    const double e2 = WGS84Ellipsoid::ECCENTRICITY_SQUARED;
    
    const double nx = cos_lat * cos_lon;
    const double ny = cos_lat * sin_lon;
    const double nz = (1.0 - e2) * sin_lat;
    
    return glm::normalize(glm::dvec3(nx, ny, nz));
}

glm::dmat4 CoordinateSystem::ENUToECEFMatrix(const GeographicCoordinates& origin) {
    const double lat_rad = origin.LatitudeRadians();
    const double lon_rad = origin.LongitudeRadians();
    
    const double sin_lat = std::sin(lat_rad);
    const double cos_lat = std::cos(lat_rad);
    const double sin_lon = std::sin(lon_rad);
    const double cos_lon = std::cos(lon_rad);
    
    // ENU to ECEF transformation matrix
    glm::dmat4 matrix(1.0);
    
    // East direction
    matrix[0][0] = -sin_lon;
    matrix[0][1] = cos_lon;
    matrix[0][2] = 0.0;
    
    // North direction
    matrix[1][0] = -sin_lat * cos_lon;
    matrix[1][1] = -sin_lat * sin_lon;
    matrix[1][2] = cos_lat;
    
    // Up direction
    matrix[2][0] = cos_lat * cos_lon;
    matrix[2][1] = cos_lat * sin_lon;
    matrix[2][2] = sin_lat;
    
    return matrix;
}

glm::dmat4 CoordinateSystem::ECEFToNUMatrix(const GeographicCoordinates& origin) {
    return glm::inverse(ENUToECEFMatrix(origin));
}

// CoordinateValidator implementation

bool CoordinateValidator::IsValidGeographic(const GeographicCoordinates& geo) {
    return geo.IsValid() && !std::isnan(geo.latitude) && !std::isnan(geo.longitude) && 
           !std::isnan(geo.altitude) && std::isfinite(geo.latitude) && 
           std::isfinite(geo.longitude) && std::isfinite(geo.altitude);
}

bool CoordinateValidator::IsValidECEF(const glm::dvec3& ecef) {
    return !std::isnan(ecef.x) && !std::isnan(ecef.y) && !std::isnan(ecef.z) &&
           std::isfinite(ecef.x) && std::isfinite(ecef.y) && std::isfinite(ecef.z);
}

bool CoordinateValidator::IsWithinEarthBounds(const glm::dvec3& ecef) {
    const double distance = glm::length(ecef);
    const double max_radius = WGS84Ellipsoid::SEMI_MAJOR_AXIS + 100000.0; // 100km buffer
    const double min_radius = WGS84Ellipsoid::SEMI_MINOR_AXIS - 1000.0;    // 1km buffer
    
    return distance >= min_radius && distance <= max_radius;
}

} // namespace earth_map