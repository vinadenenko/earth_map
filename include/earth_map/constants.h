#pragma once

/**
 * @file constants.h
 * @brief Central repository for all earth_map constants
 *
 * This file provides a single source of truth for all constants used throughout
 * the earth_map library. Constants are organized into logical namespaces for
 * clarity and maintainability.
 *
 * Design Principles:
 * - No magic numbers in implementation files
 * - Clear separation between API-level units (meters, degrees) and internal units
 * - WGS84 ellipsoid parameters for geographic calculations
 * - Normalized coordinate system (radius = 1.0) for rendering
 */

#include <cmath>

namespace earth_map {
namespace constants {

//==============================================================================
// Earth Geodetic Constants (WGS84 Ellipsoid)
//==============================================================================

/**
 * @namespace geodetic
 * @brief WGS84 ellipsoid parameters in meters
 *
 * These constants define the shape of Earth according to the WGS84 standard.
 * Used for geographic coordinate transformations and geodetic calculations.
 * Units: meters (API level)
 */
namespace geodetic {
    /// WGS84 semi-major axis (equatorial radius) in meters
    constexpr double EARTH_SEMI_MAJOR_AXIS = 6378137.0;

    /// WGS84 semi-minor axis (polar radius) in meters
    constexpr double EARTH_SEMI_MINOR_AXIS = 6356752.314245;

    /// Mean radius of Earth in meters (for simplified calculations)
    constexpr double EARTH_MEAN_RADIUS = 6371000.0;

    /// WGS84 flattening factor
    constexpr double FLATTENING = 1.0 / 298.257223563;

    /// WGS84 inverse flattening
    constexpr double INVERSE_FLATTENING = 298.257223563;

    /// WGS84 first eccentricity squared
    constexpr double ECCENTRICITY_SQUARED = 0.00669437999014;

    /// WGS84 second eccentricity squared
    constexpr double SECOND_ECCENTRICITY_SQUARED = 0.00673949674228;
} // namespace geodetic

//==============================================================================
// Rendering Constants (Internal Normalized Units)
//==============================================================================

/**
 * @namespace rendering
 * @brief Rendering system constants using normalized units
 *
 * The rendering system uses a normalized coordinate system where the globe
 * has radius = 1.0. This simplifies shader math and improves numerical precision.
 * All rendering code should use these normalized units internally.
 *
 * Conversion factor from meters to normalized units:
 *   normalized_units = meters / EARTH_MEAN_RADIUS
 */
namespace rendering {
    /// Normalized globe radius for rendering (internal units)
    constexpr float NORMALIZED_GLOBE_RADIUS = 1.0f;

    /// Default subdivision level for globe mesh
    constexpr int DEFAULT_GLOBE_SUBDIVISION = 4;

    /// Maximum subdivision level for adaptive LOD
    constexpr int MAX_GLOBE_SUBDIVISION = 8;

    /// Number of segments for simple sphere mesh (longitude divisions)
    constexpr int GLOBE_SEGMENTS = 64;

    /// Number of rings for simple sphere mesh (latitude divisions)
    constexpr int GLOBE_RINGS = 32;
} // namespace rendering

//==============================================================================
// Camera Defaults
//==============================================================================

/**
 * @namespace camera
 * @brief Default camera parameters
 *
 * These constants define the default camera behavior for both normalized
 * (rendering) and metric (API) coordinate systems.
 */
namespace camera {
    //--------------------------------------------------------------------------
    // Projection Parameters
    //--------------------------------------------------------------------------

    /// Default vertical field of view in degrees
    constexpr float DEFAULT_FOV = 45.0f;

    /// Minimum field of view in degrees
    constexpr float MIN_FOV = 1.0f;

    /// Maximum field of view in degrees
    constexpr float MAX_FOV = 179.0f;

    //--------------------------------------------------------------------------
    // Clipping Planes (Metric System - for compatibility with existing code)
    //--------------------------------------------------------------------------

    /// Default near clipping plane in meters (for metric system)
    constexpr float DEFAULT_NEAR_PLANE_METERS = 1000.0f;

    /// Default far clipping plane in meters (for metric system)
    constexpr float DEFAULT_FAR_PLANE_METERS = 10000000.0f;

    /// Minimum near clipping plane in meters
    constexpr float MIN_NEAR_PLANE_METERS = 0.001f;

    //--------------------------------------------------------------------------
    // Clipping Planes (Normalized System - for new rendering system)
    //--------------------------------------------------------------------------

    /// Default near clipping plane in normalized units
    constexpr float DEFAULT_NEAR_PLANE_NORMALIZED = 0.01f;

    /// Default far clipping plane in normalized units
    constexpr float DEFAULT_FAR_PLANE_NORMALIZED = 100.0f;

    /// Minimum near clipping plane in normalized units
    constexpr float MIN_NEAR_PLANE_NORMALIZED = 0.0001f;

    //--------------------------------------------------------------------------
    // Camera Positioning
    //--------------------------------------------------------------------------

    /// Default camera distance multiplier (distance = radius * multiplier)
    /// Note: Must be consistent with MAX_DISTANCE_NORMALIZED constraint.
    /// MAX_ALTITUDE = 10,000 km → max_distance ≈ 2.57. Using 2.5 for margin.
    constexpr float DEFAULT_CAMERA_DISTANCE_MULTIPLIER = 2.5f;

    /// Default camera distance in normalized units
    constexpr float DEFAULT_CAMERA_DISTANCE_NORMALIZED =
        rendering::NORMALIZED_GLOBE_RADIUS * DEFAULT_CAMERA_DISTANCE_MULTIPLIER;

    /// Default camera distance in meters
    constexpr float DEFAULT_CAMERA_DISTANCE_METERS =
        static_cast<float>(geodetic::EARTH_MEAN_RADIUS) * DEFAULT_CAMERA_DISTANCE_MULTIPLIER;

} // namespace camera

//==============================================================================
// Camera Constraints
//==============================================================================

/**
 * @namespace camera_constraints
 * @brief Camera movement and rotation constraints
 *
 * These constants define limits on camera movement to ensure reasonable
 * behavior and prevent degenerate cases.
 */
namespace camera_constraints {
    //--------------------------------------------------------------------------
    // Altitude Constraints (Metric System)
    //--------------------------------------------------------------------------

    /// Minimum altitude above Earth surface in meters
    constexpr float MIN_ALTITUDE_METERS = 100.0f;

    /// Maximum altitude above Earth surface in meters
    constexpr float MAX_ALTITUDE_METERS = 10000000.0f;

    /// Ground clearance in meters (for collision detection)
    constexpr float GROUND_CLEARANCE_METERS = 10.0f;

    //--------------------------------------------------------------------------
    // Distance Constraints (Normalized System)
    //--------------------------------------------------------------------------

    /// Minimum distance from globe center in normalized units
    constexpr float MIN_DISTANCE_NORMALIZED =
        rendering::NORMALIZED_GLOBE_RADIUS + (MIN_ALTITUDE_METERS / geodetic::EARTH_MEAN_RADIUS);

    /// Maximum distance from globe center in normalized units
    constexpr float MAX_DISTANCE_NORMALIZED =
        rendering::NORMALIZED_GLOBE_RADIUS + (MAX_ALTITUDE_METERS / geodetic::EARTH_MEAN_RADIUS);

    //--------------------------------------------------------------------------
    // Orientation Constraints
    //--------------------------------------------------------------------------

    /// Minimum pitch angle in degrees (looking down)
    constexpr float MIN_PITCH = -89.0f;

    /// Maximum pitch angle in degrees (looking up)
    constexpr float MAX_PITCH = 89.0f;

    //--------------------------------------------------------------------------
    // Movement Speed
    //--------------------------------------------------------------------------

    /// Maximum rotation speed in degrees per second
    constexpr float MAX_ROTATION_SPEED = 180.0f;

    /// Maximum movement speed in meters per second
    constexpr float MAX_MOVEMENT_SPEED_METERS = 1000.0f;

    /// Maximum movement speed in normalized units per second
    constexpr float MAX_MOVEMENT_SPEED_NORMALIZED =
        MAX_MOVEMENT_SPEED_METERS / static_cast<float>(geodetic::EARTH_MEAN_RADIUS);

    //--------------------------------------------------------------------------
    // Mouse/Input Sensitivity
    //--------------------------------------------------------------------------

    /// Default mouse rotation sensitivity
    constexpr float MOUSE_ROTATION_SENSITIVITY = 0.5f;

    /// Default mouse zoom sensitivity
    constexpr float MOUSE_ZOOM_SENSITIVITY = 0.1f;

    /// Default keyboard rotation sensitivity (degrees per input)
    constexpr float KEYBOARD_ROTATION_SENSITIVITY = 0.2f;

} // namespace camera_constraints

//==============================================================================
// Tile System Constants
//==============================================================================

/**
 * @namespace tiles
 * @brief Constants for tile loading and LOD calculations
 */
namespace tiles {
    /// Minimum tile zoom level
    constexpr int MIN_ZOOM_LEVEL = 0;

    /// Maximum tile zoom level
    constexpr int MAX_ZOOM_LEVEL = 21;

    /// Default tile zoom level
    constexpr int DEFAULT_ZOOM_LEVEL = 2;

    /// Tile texture size in pixels (standard web mercator tiles)
    constexpr int TILE_TEXTURE_SIZE = 256;

    /// Maximum number of tiles to keep in cache
    constexpr int MAX_CACHED_TILES = 1024;

    /// Maximum number of tiles to load per frame
    constexpr int MAX_TILES_PER_FRAME = 8;

    /// Distance threshold for tile LOD transitions (in normalized units)
    constexpr float LOD_DISTANCE_THRESHOLD = 2.0f;

} // namespace tiles

//==============================================================================
// Animation Constants
//==============================================================================

/**
 * @namespace animation
 * @brief Constants for camera animations and transitions
 */
namespace animation {
    /// Default animation duration in seconds
    constexpr float DEFAULT_DURATION = 1.0f;

    /// Default zoom animation duration in seconds
    constexpr float DEFAULT_ZOOM_DURATION = 0.5f;

    /// Default rotation animation duration in seconds
    constexpr float DEFAULT_ROTATION_DURATION = 0.8f;

    /// Minimum animation duration in seconds
    constexpr float MIN_DURATION = 0.1f;

    /// Maximum animation duration in seconds
    constexpr float MAX_DURATION = 5.0f;

} // namespace animation

//==============================================================================
// Mathematical Constants
//==============================================================================

/**
 * @namespace math
 * @brief Common mathematical constants
 */
namespace math {
    /// Pi constant
    constexpr double PI = 3.14159265358979323846;

    /// 2 * Pi
    constexpr double TWO_PI = 2.0 * PI;

    /// Pi / 2
    constexpr double HALF_PI = PI / 2.0;

    /// Degrees to radians conversion factor
    constexpr double DEG_TO_RAD = PI / 180.0;

    /// Radians to degrees conversion factor
    constexpr double RAD_TO_DEG = 180.0 / PI;

    /// Small epsilon for floating point comparisons
    constexpr float EPSILON = 1e-6f;

    /// Small epsilon for double precision comparisons
    constexpr double EPSILON_DOUBLE = 1e-10;

} // namespace math

//==============================================================================
// Unit Conversion Utilities
//==============================================================================

/**
 * @namespace conversion
 * @brief Utility functions for converting between different unit systems
 */
namespace conversion {
    /**
     * @brief Convert meters to normalized rendering units
     * @param meters Distance in meters
     * @return Distance in normalized units
     */
    constexpr inline float MetersToNormalized(float meters) {
        return meters / static_cast<float>(geodetic::EARTH_MEAN_RADIUS);
    }

    /**
     * @brief Convert normalized rendering units to meters
     * @param normalized Distance in normalized units
     * @return Distance in meters
     */
    constexpr inline float NormalizedToMeters(float normalized) {
        return normalized * static_cast<float>(geodetic::EARTH_MEAN_RADIUS);
    }

    /**
     * @brief Convert degrees to radians
     * @param degrees Angle in degrees
     * @return Angle in radians
     */
    constexpr inline double DegreesToRadians(double degrees) {
        return degrees * math::DEG_TO_RAD;
    }

    /**
     * @brief Convert radians to degrees
     * @param radians Angle in radians
     * @return Angle in degrees
     */
    constexpr inline double RadiansToDegrees(double radians) {
        return radians * math::RAD_TO_DEG;
    }

} // namespace conversion

} // namespace constants
} // namespace earth_map
