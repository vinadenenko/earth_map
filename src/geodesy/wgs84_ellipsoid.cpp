#include <earth_map/geodesy/wgs84_ellipsoid.h>
#include <earth_map/constants.h>

#include <cmath>

namespace earth_map::geodesy {
namespace {

constexpr double kHalfPi = constants::math::PI * 0.5;
constexpr double kEcefCenterEpsilonMeters = 1e-9;

[[nodiscard]] bool IsFinite(const glm::dvec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

}  // namespace

bool GeodeticPosition::IsValid() const noexcept {
    return std::isfinite(latitude_radians) && std::isfinite(longitude_radians) &&
           std::isfinite(ellipsoid_height_meters) &&
           latitude_radians >= -kHalfPi && latitude_radians <= kHalfPi &&
           longitude_radians >= -constants::math::PI &&
           longitude_radians <= constants::math::PI;
}

EcefPosition Wgs84Ellipsoid::ToEcef(const GeodeticPosition& geodetic) noexcept {
    if (!geodetic.IsValid()) {
        return {};
    }

    const double sin_latitude = std::sin(geodetic.latitude_radians);
    const double cos_latitude = std::cos(geodetic.latitude_radians);
    const double sin_longitude = std::sin(geodetic.longitude_radians);
    const double cos_longitude = std::cos(geodetic.longitude_radians);
    const double prime_vertical_radius = kSemiMajorAxisMeters /
        std::sqrt(1.0 - kFirstEccentricitySquared * sin_latitude * sin_latitude);

    return EcefPosition{glm::dvec3(
        (prime_vertical_radius + geodetic.ellipsoid_height_meters) * cos_latitude * cos_longitude,
        (prime_vertical_radius + geodetic.ellipsoid_height_meters) * cos_latitude * sin_longitude,
        (prime_vertical_radius * (1.0 - kFirstEccentricitySquared) +
            geodetic.ellipsoid_height_meters) * sin_latitude)};
}

std::optional<GeodeticPosition> Wgs84Ellipsoid::FromEcef(const EcefPosition& ecef) noexcept {
    if (!IsFinite(ecef.meters)) {
        return std::nullopt;
    }

    const double x = ecef.meters.x;
    const double y = ecef.meters.y;
    const double z = ecef.meters.z;
    const double horizontal_distance = std::hypot(x, y);
    if (horizontal_distance < kEcefCenterEpsilonMeters && std::abs(z) < kEcefCenterEpsilonMeters) {
        return std::nullopt;
    }

    const double longitude = std::atan2(y, x);
    if (horizontal_distance < kEcefCenterEpsilonMeters) {
        const double latitude = z >= 0.0 ? kHalfPi : -kHalfPi;
        return GeodeticPosition{latitude, longitude, std::abs(z) - kSemiMinorAxisMeters};
    }

    // Bowring's closed-form estimate is precise enough for WGS84 conversion;
    // two Newton refinements make the height stable for surface and orbital
    // positions without requiring a spherical approximation.
    const double auxiliary_angle = std::atan2(
        z * kSemiMajorAxisMeters,
        horizontal_distance * kSemiMinorAxisMeters);
    const double sin_auxiliary = std::sin(auxiliary_angle);
    const double cos_auxiliary = std::cos(auxiliary_angle);
    double latitude = std::atan2(
        z + kSecondEccentricitySquared * kSemiMinorAxisMeters *
                sin_auxiliary * sin_auxiliary * sin_auxiliary,
        horizontal_distance - kFirstEccentricitySquared * kSemiMajorAxisMeters *
                cos_auxiliary * cos_auxiliary * cos_auxiliary);

    double height = 0.0;
    for (int iteration = 0; iteration < 2; ++iteration) {
        const double sin_latitude = std::sin(latitude);
        const double cos_latitude = std::cos(latitude);
        const double prime_vertical_radius = kSemiMajorAxisMeters /
            std::sqrt(1.0 - kFirstEccentricitySquared * sin_latitude * sin_latitude);
        height = horizontal_distance / cos_latitude - prime_vertical_radius;
        latitude = std::atan2(
            z,
            horizontal_distance *
                (1.0 - kFirstEccentricitySquared * prime_vertical_radius /
                    (prime_vertical_radius + height)));
    }

    const double sin_latitude = std::sin(latitude);
    const double prime_vertical_radius = kSemiMajorAxisMeters /
        std::sqrt(1.0 - kFirstEccentricitySquared * sin_latitude * sin_latitude);
    height = horizontal_distance / std::cos(latitude) - prime_vertical_radius;
    return GeodeticPosition{latitude, longitude, height};
}

glm::dvec3 Wgs84Ellipsoid::SurfaceNormal(const GeodeticPosition& geodetic) noexcept {
    if (!geodetic.IsValid()) {
        return glm::dvec3(0.0);
    }

    const double cos_latitude = std::cos(geodetic.latitude_radians);
    return glm::dvec3(
        cos_latitude * std::cos(geodetic.longitude_radians),
        cos_latitude * std::sin(geodetic.longitude_radians),
        std::sin(geodetic.latitude_radians));
}

EnuFrame Wgs84Ellipsoid::MakeEnuFrame(const GeodeticPosition& origin) noexcept {
    const double sin_latitude = std::sin(origin.latitude_radians);
    const double cos_latitude = std::cos(origin.latitude_radians);
    const double sin_longitude = std::sin(origin.longitude_radians);
    const double cos_longitude = std::cos(origin.longitude_radians);

    return EnuFrame{
        ToEcef(origin),
        glm::dvec3(-sin_longitude, cos_longitude, 0.0),
        glm::dvec3(-sin_latitude * cos_longitude, -sin_latitude * sin_longitude, cos_latitude),
        glm::dvec3(cos_latitude * cos_longitude, cos_latitude * sin_longitude, sin_latitude)};
}

glm::dvec3 Wgs84Ellipsoid::ToEnu(const EcefPosition& ecef, const EnuFrame& frame) noexcept {
    const glm::dvec3 delta = ecef.meters - frame.origin.meters;
    return glm::dvec3(
        glm::dot(delta, frame.east),
        glm::dot(delta, frame.north),
        glm::dot(delta, frame.up));
}

EcefPosition Wgs84Ellipsoid::FromEnu(
    const glm::dvec3& enu_meters,
    const EnuFrame& frame) noexcept {
    return EcefPosition{frame.origin.meters +
        enu_meters.x * frame.east +
        enu_meters.y * frame.north +
        enu_meters.z * frame.up};
}

}  // namespace earth_map::geodesy

