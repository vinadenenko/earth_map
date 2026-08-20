/**
 * @file wgs84_ellipsoid.h
 * @brief Authoritative WGS84 geodetic, ECEF, and local-ENU conversions.
 *
 * This module deliberately does not depend on the renderer's historical
 * normalized-sphere `coordinates::World` type.  New globe-selection and
 * terrain code must use these metre/radian types as their physical model.
 */

#pragma once

#include <glm/glm.hpp>

#include <optional>

namespace earth_map::geodesy {

/**
 * WGS84 geodetic position.
 *
 * Latitude and longitude are radians.  Height is metres above the WGS84
 * reference ellipsoid.  Orthometric (mean-sea-level) or terrain-relative
 * source heights must be converted before constructing this type.
 */
struct GeodeticPosition final {
    double latitude_radians = 0.0;
    double longitude_radians = 0.0;
    double ellipsoid_height_meters = 0.0;

    [[nodiscard]] bool IsValid() const noexcept;
};

/** Earth-centred, Earth-fixed Cartesian position in metres. */
struct EcefPosition final {
    glm::dvec3 meters{0.0};
};

/** A right-handed east-north-up frame whose origin is on or above WGS84. */
struct EnuFrame final {
    EcefPosition origin;
    glm::dvec3 east{1.0, 0.0, 0.0};
    glm::dvec3 north{0.0, 1.0, 0.0};
    glm::dvec3 up{0.0, 0.0, 1.0};
};

/**
 * WGS84 reference ellipsoid.
 *
 * This is the only physical Earth model for new globe/terrain work.  Web
 * Mercator is an imagery tile-matrix projection and intentionally is not part
 * of this class.
 */
class Wgs84Ellipsoid final {
public:
    static constexpr double kSemiMajorAxisMeters = 6378137.0;
    static constexpr double kInverseFlattening = 298.257223563;
    static constexpr double kFlattening = 1.0 / kInverseFlattening;
    static constexpr double kSemiMinorAxisMeters =
        kSemiMajorAxisMeters * (1.0 - kFlattening);
    static constexpr double kFirstEccentricitySquared =
        kFlattening * (2.0 - kFlattening);
    static constexpr double kSecondEccentricitySquared =
        kFirstEccentricitySquared / (1.0 - kFirstEccentricitySquared);

    [[nodiscard]] static EcefPosition ToEcef(const GeodeticPosition& geodetic) noexcept;

    /**
     * Converts an ECEF metre position to WGS84 geodetic coordinates.
     * Returns `nullopt` only for the mathematically undefined Earth-centre
     * position or non-finite input.
     */
    [[nodiscard]] static std::optional<GeodeticPosition> FromEcef(
        const EcefPosition& ecef) noexcept;

    /** Outward unit normal of the WGS84 ellipsoid at a geodetic position. */
    [[nodiscard]] static glm::dvec3 SurfaceNormal(const GeodeticPosition& geodetic) noexcept;

    [[nodiscard]] static EnuFrame MakeEnuFrame(const GeodeticPosition& origin) noexcept;

    /** Converts an ECEF position to local ENU metres in `frame`. */
    [[nodiscard]] static glm::dvec3 ToEnu(
        const EcefPosition& ecef,
        const EnuFrame& frame) noexcept;

    /** Converts local ENU metres in `frame` back to ECEF metres. */
    [[nodiscard]] static EcefPosition FromEnu(
        const glm::dvec3& enu_meters,
        const EnuFrame& frame) noexcept;
};

}  // namespace earth_map::geodesy

