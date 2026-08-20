#include <gtest/gtest.h>

#include <earth_map/geodesy/wgs84_ellipsoid.h>

#include <cmath>
#include <numbers>

namespace earth_map::geodesy {
namespace {

constexpr double DegreesToRadians(double degrees) {
    return degrees * std::numbers::pi_v<double> / 180.0;
}

}  // namespace

TEST(Wgs84EllipsoidTest, EquatorPrimeMeridianMapsToSemiMajorAxis) {
    const EcefPosition ecef = Wgs84Ellipsoid::ToEcef({0.0, 0.0, 0.0});

    EXPECT_NEAR(ecef.meters.x, Wgs84Ellipsoid::kSemiMajorAxisMeters, 1e-6);
    EXPECT_NEAR(ecef.meters.y, 0.0, 1e-9);
    EXPECT_NEAR(ecef.meters.z, 0.0, 1e-9);
}

TEST(Wgs84EllipsoidTest, NorthPoleMapsToSemiMinorAxis) {
    const EcefPosition ecef = Wgs84Ellipsoid::ToEcef({
        std::numbers::pi_v<double> * 0.5, 0.0, 0.0});

    EXPECT_NEAR(ecef.meters.x, 0.0, 1e-6);
    EXPECT_NEAR(ecef.meters.y, 0.0, 1e-6);
    EXPECT_NEAR(ecef.meters.z, Wgs84Ellipsoid::kSemiMinorAxisMeters, 1e-6);
}

TEST(Wgs84EllipsoidTest, GeodeticEcefRoundTripPreservesYerevanAndHeight) {
    const GeodeticPosition original{
        DegreesToRadians(40.1872), DegreesToRadians(44.5152), 1247.5};

    const std::optional<GeodeticPosition> recovered =
        Wgs84Ellipsoid::FromEcef(Wgs84Ellipsoid::ToEcef(original));

    ASSERT_TRUE(recovered.has_value());
    EXPECT_NEAR(recovered->latitude_radians, original.latitude_radians, 1e-12);
    EXPECT_NEAR(recovered->longitude_radians, original.longitude_radians, 1e-12);
    EXPECT_NEAR(recovered->ellipsoid_height_meters, original.ellipsoid_height_meters, 1e-5);
}

TEST(Wgs84EllipsoidTest, GeodeticEcefRoundTripPreservesHighAltitudePosition) {
    const GeodeticPosition original{
        DegreesToRadians(-33.8688), DegreesToRadians(151.2093), 408000.0};

    const std::optional<GeodeticPosition> recovered =
        Wgs84Ellipsoid::FromEcef(Wgs84Ellipsoid::ToEcef(original));

    ASSERT_TRUE(recovered.has_value());
    EXPECT_NEAR(recovered->latitude_radians, original.latitude_radians, 1e-12);
    EXPECT_NEAR(recovered->longitude_radians, original.longitude_radians, 1e-12);
    EXPECT_NEAR(recovered->ellipsoid_height_meters, original.ellipsoid_height_meters, 1e-5);
}

TEST(Wgs84EllipsoidTest, EnuFrameRoundTripsLocalMetres) {
    const GeodeticPosition origin{
        DegreesToRadians(40.1872), DegreesToRadians(44.5152), 1000.0};
    const EnuFrame frame = Wgs84Ellipsoid::MakeEnuFrame(origin);
    const glm::dvec3 expected_enu(123.25, -87.5, 42.0);

    const EcefPosition ecef = Wgs84Ellipsoid::FromEnu(expected_enu, frame);
    const glm::dvec3 actual_enu = Wgs84Ellipsoid::ToEnu(ecef, frame);

    EXPECT_NEAR(actual_enu.x, expected_enu.x, 1e-9);
    EXPECT_NEAR(actual_enu.y, expected_enu.y, 1e-9);
    EXPECT_NEAR(actual_enu.z, expected_enu.z, 1e-9);
}

TEST(Wgs84EllipsoidTest, EarthCentreHasNoGeodeticRepresentation) {
    EXPECT_FALSE(Wgs84Ellipsoid::FromEcef(EcefPosition{}).has_value());
}

}  // namespace earth_map::geodesy

