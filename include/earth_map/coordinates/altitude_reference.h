/**
 * @file altitude_reference.h
 * @brief Altitude reference systems and conversion utilities
 *
 * Defines altitude reference datums and conversion utilities for working with
 * different height systems (WGS84 ellipsoid, mean sea level, terrain, etc.).
 *
 * This is critical for SRTM terrain integration where heights are referenced
 * to the WGS84 ellipsoid.
 *
 * @see coordinate_spaces.h for Geographic coordinate definition
 */

#pragma once

#include <cstdint>

namespace earth_map {
namespace coordinates {

// Forward declaration
struct Geographic;

/**
 * @brief Altitude reference datum types
 *
 * Defines the reference surface from which altitude is measured.
 */
enum class AltitudeReference : uint8_t {
    /**
     * @brief Height above WGS84 ellipsoid (default)
     *
     * Reference surface: WGS84 ellipsoid (oblate spheroid)
     * - Semi-major axis: 6378137.0 m
     * - Semi-minor axis: 6356752.314245 m
     *
     * **Usage**: Default for GPS, SRTM terrain data, satellite measurements
     *
     * **Note**: This is the most common reference for digital elevation data.
     * SRTM (Shuttle Radar Topography Mission) heights are referenced to the
     * WGS84 ellipsoid.
     */
    WGS84_ELLIPSOID = 0,

    /**
     * @brief Height above geoid (Mean Sea Level)
     *
     * Reference surface: EGM96 or EGM2008 geoid model
     *
     * **Usage**: Orthometric heights, mapping, surveying
     *
     * **Note**: The geoid is an equipotential surface that approximates mean
     * sea level. It differs from the WGS84 ellipsoid by ±100m globally.
     *
     * Conversion requires geoid height model (EGM96/EGM2008).
     */
    MEAN_SEA_LEVEL = 1,

    /**
     * @brief Height above local terrain surface
     *
     * Reference surface: Terrain elevation at location
     *
     * **Usage**: Above-ground-level (AGL) heights, aviation, drones
     *
     * **Example**: Building height, tree height, aircraft altitude AGL
     *
     * **Note**: Requires terrain elevation data to convert to other references.
     */
    TERRAIN = 2,

    /**
     * @brief Absolute distance from Earth's center (geocentric)
     *
     * Reference point: Earth's center of mass
     *
     * **Usage**: Orbital mechanics, satellite positioning, ECEF coordinates
     *
     * **Note**: This is the radial distance in ECEF (Earth-Centered Earth-Fixed)
     * coordinate system.
     */
    ABSOLUTE = 3
};

/**
 * @brief Altitude conversion utilities
 *
 * Provides functions to convert altitude between different reference systems.
 *
 * **Important**: Conversions involving MEAN_SEA_LEVEL require a geoid model
 * (EGM96 or EGM2008). Without a geoid model, these conversions will use an
 * approximate offset based on global average (~0m at equator).
 */
class AltitudeConverter {
public:
    /**
     * @brief Convert altitude from one reference to another
     *
     * @param altitude Altitude value in meters
     * @param from Source altitude reference
     * @param to Target altitude reference
     * @param location Geographic location (lat/lon) - required for reference-dependent conversions
     * @param terrain_elevation Terrain elevation in meters above WGS84 ellipsoid (required for TERRAIN conversions)
     * @return double Converted altitude in meters
     *
     * **Example**:
     * ```cpp
     * // Convert SRTM elevation (WGS84 ellipsoid) to height above sea level
     * double srtm_height = 1523.5;  // meters above WGS84
     * Geographic location(40.7128, -74.0060, 0.0);  // NYC
     * double msl_height = AltitudeConverter::Convert(
     *     srtm_height,
     *     AltitudeReference::WGS84_ELLIPSOID,
     *     AltitudeReference::MEAN_SEA_LEVEL,
     *     location
     * );
     * ```
     *
     * **Note**: Conversions involving MEAN_SEA_LEVEL use approximate geoid heights
     * until a full geoid model (EGM96/EGM2008) is integrated.
     */
    static double Convert(
        double altitude,
        AltitudeReference from,
        AltitudeReference to,
        const Geographic& location,
        double terrain_elevation = 0.0
    ) noexcept;

    /**
     * @brief Get geoid height (undulation) at a location
     *
     * Returns the height of the geoid (mean sea level) above the WGS84 ellipsoid.
     *
     * @param location Geographic location
     * @return double Geoid height in meters (positive = geoid above ellipsoid)
     *
     * **Note**: Currently returns approximate values. Will be updated when full
     * geoid model (EGM96/EGM2008) is integrated.
     *
     * **Range**: Typically -100m to +100m globally
     */
    static double GetGeoidHeight(const Geographic& location) noexcept;

    /**
     * @brief Get WGS84 ellipsoid radius at a given latitude
     *
     * Calculates the distance from Earth's center to the WGS84 ellipsoid surface
     * at the given latitude.
     *
     * @param latitude_degrees Latitude in degrees
     * @return double Ellipsoid radius in meters
     *
     * **Formula**: Uses parametric latitude for accurate ellipsoid radius calculation
     */
    static double GetEllipsoidRadius(double latitude_degrees) noexcept;

    /**
     * @brief Calculate geocentric radius (distance from Earth's center)
     *
     * Converts altitude above WGS84 ellipsoid to absolute distance from Earth's center.
     *
     * @param location Geographic location with altitude
     * @return double Geocentric radius in meters
     */
    static double GetGeocentricRadius(const Geographic& location) noexcept;

private:
    // WGS84 ellipsoid constants
    static constexpr double WGS84_SEMI_MAJOR_AXIS = 6378137.0;         ///< meters
    static constexpr double WGS84_SEMI_MINOR_AXIS = 6356752.314245;    ///< meters
    static constexpr double WGS84_FLATTENING = 1.0 / 298.257223563;
    static constexpr double WGS84_ECCENTRICITY_SQ = 0.00669437999014;

    // Mean Earth radius for approximations
    static constexpr double EARTH_MEAN_RADIUS = 6371000.0;             ///< meters
};

/**
 * @brief String conversion utilities for altitude references
 */
namespace altitude_reference_utils {

    /**
     * @brief Convert AltitudeReference to string
     */
    inline const char* ToString(AltitudeReference ref) noexcept {
        switch (ref) {
            case AltitudeReference::WGS84_ELLIPSOID: return "WGS84 Ellipsoid";
            case AltitudeReference::MEAN_SEA_LEVEL:  return "Mean Sea Level";
            case AltitudeReference::TERRAIN:         return "Terrain";
            case AltitudeReference::ABSOLUTE:        return "Absolute (Geocentric)";
            default:                                  return "Unknown";
        }
    }

    /**
     * @brief Convert AltitudeReference to short code
     */
    inline const char* ToCode(AltitudeReference ref) noexcept {
        switch (ref) {
            case AltitudeReference::WGS84_ELLIPSOID: return "WGS84";
            case AltitudeReference::MEAN_SEA_LEVEL:  return "MSL";
            case AltitudeReference::TERRAIN:         return "AGL";
            case AltitudeReference::ABSOLUTE:        return "ABS";
            default:                                  return "UNK";
        }
    }

} // namespace altitude_reference_utils

} // namespace coordinates
} // namespace earth_map
