#pragma once

/**
 * @file coordinate_system.h
 * @brief Coordinate system transformations and utilities
 * 
 * Defines coordinate transformations between geographic (lat/lon),
 * Cartesian (ECEF), and projected coordinate systems.
 * Includes WGS84 ellipsoid model and conversion functions.
 */

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace earth_map {

/**
 * @brief Geographic coordinates (latitude, longitude, altitude)
 */
struct GeographicCoordinates {
    double latitude;   ///< Latitude in degrees (-90 to +90)
    double longitude;  ///< Longitude in degrees (-180 to +180)
    double altitude;   ///< Altitude in meters above mean sea level
    
    /**
     * @brief Default constructor
     */
    constexpr GeographicCoordinates() 
        : latitude(0.0), longitude(0.0), altitude(0.0) {}
    
    /**
     * @brief Construct from lat/lon/alt
     * 
     * @param lat Latitude in degrees
     * @param lon Longitude in degrees
     * @param alt Altitude in meters (default: 0)
     */
    constexpr GeographicCoordinates(double lat, double lon, double alt = 0.0)
        : latitude(lat), longitude(lon), altitude(alt) {}
    
    /**
     * @brief Check if coordinates are valid
     * 
     * @return true if latitude and longitude are in valid ranges
     */
    constexpr bool IsValid() const {
        return latitude >= -90.0 && latitude <= 90.0 &&
               longitude >= -180.0 && longitude <= 180.0;
    }
    
    /**
     * @brief Normalize longitude to [-180, 180] range
     */
    void NormalizeLongitude() {
        while (longitude > 180.0) longitude -= 360.0;
        while (longitude < -180.0) longitude += 360.0;
    }
    
    /**
     * @brief Convert latitude to radians
     */
    constexpr double LatitudeRadians() const {
        return latitude * M_PI / 180.0;
    }
    
    /**
     * @brief Convert longitude to radians
     */
    constexpr double LongitudeRadians() const {
        return longitude * M_PI / 180.0;
    }
};

/**
 * @brief WGS84 Ellipsoid parameters
 */
struct WGS84Ellipsoid {
    static constexpr double SEMI_MAJOR_AXIS = 6378137.0;        ///< Semi-major axis (meters)
    static constexpr double SEMI_MINOR_AXIS = 6356752.314245;   ///< Semi-minor axis (meters)
    static constexpr double FLATTENING = 1.0 / 298.257223563;  ///< Flattening
    static constexpr double INVERSE_FLATTENING = 298.257223563; ///< Inverse flattening
    static constexpr double ECCENTRICITY_SQUARED = 0.00669437999014; ///< First eccentricity squared
    static constexpr double SECOND_ECCENTRICITY_SQUARED = 0.00673949674228; ///< Second eccentricity squared
    
    /**
     * @brief Calculate the radius of curvature in the prime vertical
     * 
     * @param latitude Latitude in radians
     * @return double Radius of curvature in meters
     */
    static double RadiusOfCurvaturePrimeVertical(double latitude) {
        const double sin_lat = std::sin(latitude);
        const double e2 = ECCENTRICITY_SQUARED;
        return SEMI_MAJOR_AXIS / std::sqrt(1.0 - e2 * sin_lat * sin_lat);
    }
    
    /**
     * @brief Calculate the radius of curvature in the meridian
     * 
     * @param latitude Latitude in radians
     * @return double Radius of curvature in meters
     */
    static double RadiusOfCurvatureMeridian(double latitude) {
        const double sin_lat = std::sin(latitude);
        const double cos_lat = std::cos(latitude);
        const double a = SEMI_MAJOR_AXIS;
        const double b = SEMI_MINOR_AXIS;
        return (a * a * b) / std::pow((a * a * cos_lat * cos_lat + b * b * sin_lat * sin_lat), 1.5);
    }
};

/**
 * @brief Coordinate system transformation utilities
 */
class CoordinateSystem {
public:
    /**
     * @brief Convert geographic coordinates to ECEF (Earth-Centered, Earth-Fixed)
     * 
     * @param geo Geographic coordinates
     * @return glm::dvec3 ECEF coordinates in meters
     */
    static glm::dvec3 GeographicToECEF(const GeographicCoordinates& geo);
    
    /**
     * @brief Convert ECEF coordinates to geographic coordinates
     * 
     * @param ecef ECEF coordinates in meters
     * @return GeographicCoordinates Geographic coordinates
     */
    static GeographicCoordinates ECEFToGeographic(const glm::dvec3& ecef);
    
    /**
     * @brief Convert geographic coordinates to local ENU (East-North-Up) coordinates
     * 
     * @param geo Point to convert
     * @param origin Origin of the local ENU coordinate system
     * @return glm::dvec3 ENU coordinates in meters
     */
    static glm::dvec3 GeographicToENU(const GeographicCoordinates& geo, 
                                     const GeographicCoordinates& origin);
    
    /**
     * @brief Convert ENU coordinates to geographic coordinates
     * 
     * @param enu ENU coordinates in meters
     * @param origin Origin of the local ENU coordinate system
     * @return GeographicCoordinates Geographic coordinates
     */
    static GeographicCoordinates ENUToGeographic(const glm::dvec3& enu, 
                                               const GeographicCoordinates& origin);
    
    /**
     * @brief Calculate surface normal at a geographic location
     * 
     * @param geo Geographic coordinates
     * @return glm::dvec3 Surface normal (unit vector)
     */
    static glm::dvec3 SurfaceNormal(const GeographicCoordinates& geo);
    
    /**
     * @brief Calculate the transformation matrix from ENU to ECEF
     * 
     * @param origin Origin of the local ENU coordinate system
     * @return glm::dmat4 4x4 transformation matrix
     */
    static glm::dmat4 ENUToECEFMatrix(const GeographicCoordinates& origin);
    
    /**
     * @brief Calculate the transformation matrix from ECEF to ENU
     * 
     * @param origin Origin of the local ENU coordinate system
     * @return glm::dmat4 4x4 transformation matrix
     */
    static glm::dmat4 ECEFToNUMatrix(const GeographicCoordinates& origin);
    
    /**
     * @brief Convert degrees to radians
     * 
     * @param degrees Angle in degrees
     * @return double Angle in radians
     */
    static constexpr double DegreesToRadians(double degrees) {
        return degrees * M_PI / 180.0;
    }
    
    /**
     * @brief Convert radians to degrees
     * 
     * @param radians Angle in radians
     * @return double Angle in degrees
     */
    static constexpr double RadiansToDegrees(double radians) {
        return radians * 180.0 / M_PI;
    }
    
    /**
     * @brief Normalize angle to [-180, 180] degrees
     * 
     * @param angle Angle in degrees
     * @return double Normalized angle
     */
    static double NormalizeAngle(double angle) {
        while (angle > 180.0) angle -= 360.0;
        while (angle < -180.0) angle += 360.0;
        return angle;
    }
    
    /**
     * @brief Normalize angle to [-π, π] radians
     * 
     * @param angle Angle in radians
     * @return double Normalized angle
     */
    static double NormalizeAngleRadians(double angle) {
        while (angle > M_PI) angle -= 2.0 * M_PI;
        while (angle < -M_PI) angle += 2.0 * M_PI;
        return angle;
    }
};

/**
 * @brief Coordinate system validation utilities
 */
class CoordinateValidator {
public:
    /**
     * @brief Validate geographic coordinates
     * 
     * @param geo Geographic coordinates to validate
     * @return true if coordinates are valid, false otherwise
     */
    static bool IsValidGeographic(const GeographicCoordinates& geo);
    
    /**
     * @brief Validate ECEF coordinates
     * 
     * @param ecef ECEF coordinates to validate
     * @return true if coordinates are valid, false otherwise
     */
    static bool IsValidECEF(const glm::dvec3& ecef);
    
    /**
     * @brief Check if coordinates are within Earth's bounds
     * 
     * @param ecef ECEF coordinates
     * @return true if within reasonable Earth bounds, false otherwise
     */
    static bool IsWithinEarthBounds(const glm::dvec3& ecef);
};

} // namespace earth_map