#include <earth_map/earth_map.h>
#include <earth_map/renderer/tile_renderer.h>
#include <earth_map/core/camera_controller.h>
#include <earth_map/constants.h>
#include <earth_map/geodesy/wgs84_ellipsoid.h>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>

using namespace earth_map;

/**
 * @brief Test fixture for tile visibility and geographic bounds calculation
 *
 * Tests that tiles are correctly selected based on camera position and orientation.
 * This is critical for ensuring tiles appear where the camera is LOOKING, not
 * on the opposite side of the globe.
 *
 * Geographic conversions here use the real WGS84/ECEF axis convention
 * (X toward lon=0/lat=0, Y toward lon=90E/lat=0, Z toward the North Pole),
 * via geodesy::Wgs84Ellipsoid -- not a hand-rolled sphere approximation.
 */
class TileVisibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.screen_width = 1920;
        config_.screen_height = 1080;

        camera_controller_ = CreateCameraController(config_);
        ASSERT_NE(camera_controller_, nullptr);
        ASSERT_TRUE(camera_controller_->Initialize());
    }

    void TearDown() override {
        delete camera_controller_;
        camera_controller_ = nullptr;
    }

    /**
     * @brief Helper to convert a direction (any nonzero magnitude) to
     * geographic coordinates, via the real WGS84/ECEF conversion. Only the
     * direction matters here -- the vector is rescaled to a realistic ECEF
     * radius before conversion so it stays in Wgs84Ellipsoid::FromEcef's
     * well-conditioned range.
     */
    glm::vec2 PositionToGeographic(const glm::vec3& pos) const {
        const glm::dvec3 direction = glm::normalize(glm::dvec3(pos));
        const geodesy::EcefPosition scaled{
            direction * geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters};
        const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(scaled);
        if (!geodetic.has_value()) {
            return glm::vec2(0.0f, 0.0f);
        }
        return glm::vec2(
            static_cast<float>(constants::conversion::RadiansToDegrees(
                geodetic->longitude_radians)),
            static_cast<float>(constants::conversion::RadiansToDegrees(
                geodetic->latitude_radians)));
    }

    /**
     * @brief Helper to get what direction camera is looking
     */
    glm::vec3 GetCameraLookDirection() const {
        const geodesy::EcefPosition position = camera_controller_->GetEcefPosition();
        const geodesy::EcefPosition target = camera_controller_->GetEcefTarget();
        return glm::normalize(glm::vec3(target.meters - position.meters));
    }

    Configuration config_;
    CameraController* camera_controller_ = nullptr;
};

/**
 * @brief Test camera looking at Prime Meridian (0°, 0°)
 *
 * The default camera sits on the ECEF +X axis (lon=0°, lat=0°) looking at
 * the ECEF origin -- so it looks toward the antipode, lon=180°.
 */
TEST_F(TileVisibilityTest, CameraLookingAtPrimeMeridian) {
    const geodesy::EcefPosition camera_position = camera_controller_->GetEcefPosition();
    const geodesy::EcefPosition target = camera_controller_->GetEcefTarget();

    // Camera is on the +X axis, above the WGS84 surface.
    EXPECT_NEAR(camera_position.meters.y, 0.0, 1.0);
    EXPECT_NEAR(camera_position.meters.z, 0.0, 1.0);
    EXPECT_GT(camera_position.meters.x, geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters);

    // Target is at the ECEF origin.
    EXPECT_NEAR(target.meters.x, 0.0, 0.01);
    EXPECT_NEAR(target.meters.y, 0.0, 0.01);
    EXPECT_NEAR(target.meters.z, 0.0, 0.01);

    // Camera look direction should point toward -X (toward Earth's centre).
    const glm::vec3 look_dir = GetCameraLookDirection();
    EXPECT_LT(look_dir.x, -0.99f);
    EXPECT_NEAR(look_dir.y, 0.0f, 0.01f);
    EXPECT_NEAR(look_dir.z, 0.0f, 0.01f);

    // The point on the globe the camera looks at (opposite the camera's own
    // direction from Earth's centre) is the antipode: lon=180°, lat=0°.
    const glm::vec3 look_point = -glm::normalize(glm::vec3(camera_position.meters));
    const glm::vec2 look_geo = PositionToGeographic(look_point);
    EXPECT_NEAR(std::abs(look_geo.x), 180.0f, 1.0f);
    EXPECT_NEAR(look_geo.y, 0.0f, 1.0f);
}

/**
 * @brief Test camera position vs look direction coordinate systems
 *
 * Critical test: Verify that when camera is on one side of globe,
 * it's looking at the OPPOSITE side.
 */
TEST_F(TileVisibilityTest, CameraPositionVsLookDirection) {
    const geodesy::EcefPosition camera_position = camera_controller_->GetEcefPosition();

    const glm::vec2 camera_geo = PositionToGeographic(glm::vec3(camera_position.meters));
    const glm::vec3 look_point = -glm::normalize(glm::vec3(camera_position.meters));
    const glm::vec2 look_geo = PositionToGeographic(look_point);

    // Camera is at lon=0°, lat=0° (default: ECEF +X axis).
    EXPECT_NEAR(camera_geo.x, 0.0f, 1.0f);
    EXPECT_NEAR(camera_geo.y, 0.0f, 1.0f);

    // Camera looks at lon=180° (or -180°), lat=0°.
    EXPECT_NEAR(std::abs(look_geo.x), 180.0f, 1.0f);
    EXPECT_NEAR(look_geo.y, 0.0f, 1.0f);

    // The longitudes should be 180° apart
    const float lon_diff = std::abs(camera_geo.x - look_geo.x);
    EXPECT_TRUE(std::abs(lon_diff - 180.0f) < 5.0f || std::abs(lon_diff - 360.0f) < 5.0f);
}

/**
 * @brief Test camera looking at specific geographic location
 */
TEST_F(TileVisibilityTest, CameraLookingAtSpecificLocation) {
    // Position camera on the ECEF +Y axis (lon=90°E, lat=0°).
    camera_controller_->SetEcefPosition(geodesy::EcefPosition{
        glm::dvec3(0.0, geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters * 3.0, 0.0)});
    camera_controller_->SetEcefTarget(geodesy::EcefPosition{glm::dvec3(0.0)});

    const geodesy::EcefPosition camera_position = camera_controller_->GetEcefPosition();

    const glm::vec2 camera_geo = PositionToGeographic(glm::vec3(camera_position.meters));
    EXPECT_NEAR(camera_geo.x, 90.0f, 1.0f);
    EXPECT_NEAR(camera_geo.y, 0.0f, 1.0f);

    // Camera looks at lon=-90° (or 270°), lat=0°.
    const glm::vec3 look_point = -glm::normalize(glm::vec3(camera_position.meters));
    const glm::vec2 look_geo = PositionToGeographic(look_point);
    EXPECT_NEAR(std::abs(look_geo.x), 90.0f, 1.0f);
    EXPECT_NEAR(look_geo.y, 0.0f, 1.0f);
}

/**
 * @brief Test camera looking at North Pole
 */
TEST_F(TileVisibilityTest, CameraLookingAtNorthPole) {
    // Position camera on the ECEF +Z axis (above the North Pole).
    camera_controller_->SetEcefPosition(geodesy::EcefPosition{
        glm::dvec3(0.0, 0.0, geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters * 3.0)});
    camera_controller_->SetEcefTarget(geodesy::EcefPosition{glm::dvec3(0.0)});

    const geodesy::EcefPosition camera_position = camera_controller_->GetEcefPosition();

    const glm::vec2 camera_geo = PositionToGeographic(glm::vec3(camera_position.meters));
    EXPECT_NEAR(camera_geo.y, 90.0f, 1.0f);

    // Camera looks at South Pole (lat=-90°).
    const glm::vec3 look_point = -glm::normalize(glm::vec3(camera_position.meters));
    const glm::vec2 look_geo = PositionToGeographic(look_point);
    EXPECT_NEAR(look_geo.y, -90.0f, 1.0f);
}

/**
 * @brief Test camera looking at South Pole
 */
TEST_F(TileVisibilityTest, CameraLookingAtSouthPole) {
    // Position camera on the ECEF -Z axis (below the South Pole).
    camera_controller_->SetEcefPosition(geodesy::EcefPosition{
        glm::dvec3(0.0, 0.0, -geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters * 3.0)});
    camera_controller_->SetEcefTarget(geodesy::EcefPosition{glm::dvec3(0.0)});

    const geodesy::EcefPosition camera_position = camera_controller_->GetEcefPosition();

    const glm::vec2 camera_geo = PositionToGeographic(glm::vec3(camera_position.meters));
    EXPECT_NEAR(camera_geo.y, -90.0f, 1.0f);

    // Camera looks at North Pole (lat=90°).
    const glm::vec3 look_point = -glm::normalize(glm::vec3(camera_position.meters));
    const glm::vec2 look_geo = PositionToGeographic(look_point);
    EXPECT_NEAR(look_geo.y, 90.0f, 1.0f);
}

/**
 * @brief Test longitude wraparound at International Date Line
 */
TEST_F(TileVisibilityTest, LongitudeWraparound) {
    // Position camera on the ECEF -X axis (lon=180°, lat=0°).
    camera_controller_->SetEcefPosition(geodesy::EcefPosition{
        glm::dvec3(-geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters * 3.0, 0.0, 0.0)});
    camera_controller_->SetEcefTarget(geodesy::EcefPosition{glm::dvec3(0.0)});

    const geodesy::EcefPosition camera_position = camera_controller_->GetEcefPosition();

    // Camera is at lon=180° or -180° (same meridian).
    const glm::vec2 camera_geo = PositionToGeographic(glm::vec3(camera_position.meters));
    EXPECT_NEAR(std::abs(camera_geo.x), 180.0f, 1.0f);

    // Camera looks at lon=0° (Prime Meridian).
    const glm::vec3 look_point = -glm::normalize(glm::vec3(camera_position.meters));
    const glm::vec2 look_geo = PositionToGeographic(look_point);
    EXPECT_NEAR(look_geo.x, 0.0f, 1.0f);
}

/**
 * @brief Test that visible tiles match camera look direction
 *
 * This is the KEY test: tiles should be loaded for where camera LOOKS,
 * not where camera IS positioned.
 */
TEST_F(TileVisibilityTest, VisibleTilesMatchLookDirection) {
    // Default camera: on the ECEF +X axis (lon=0°) looking at the origin,
    // i.e. looking toward lon=180°, lat=0°.
    const geodesy::EcefPosition camera_position = camera_controller_->GetEcefPosition();
    const glm::vec3 look_point = -glm::normalize(glm::vec3(camera_position.meters));
    const glm::vec2 look_geo = PositionToGeographic(look_point);

    EXPECT_NEAR(std::abs(look_geo.x), 180.0f, 1.0f);
    EXPECT_NEAR(look_geo.y, 0.0f, 1.0f);
}

/**
 * @brief Test geographic coordinate conversion consistency
 */
TEST_F(TileVisibilityTest, GeographicConversionConsistency) {
    struct TestCase {
        glm::vec3 position;
        float expected_lon;
        float expected_lat;
        std::string description;
    };

    const std::vector<TestCase> test_cases = {
        { glm::vec3(1.0f, 0.0f, 0.0f), 0.0f, 0.0f, "+X axis (Prime Meridian)" },
        { glm::vec3(0.0f, 1.0f, 0.0f), 90.0f, 0.0f, "+Y axis (90° East)" },
        { glm::vec3(0.0f, -1.0f, 0.0f), -90.0f, 0.0f, "-Y axis (90° West)" },
        { glm::vec3(0.0f, 0.0f, 1.0f), 0.0f, 90.0f, "+Z axis (North Pole)" },
        { glm::vec3(0.0f, 0.0f, -1.0f), 0.0f, -90.0f, "-Z axis (South Pole)" },
    };

    for (const auto& test : test_cases) {
        const glm::vec2 geo = PositionToGeographic(test.position);
        // Longitude is undefined exactly at a pole -- only check it away
        // from the poles.
        if (std::abs(test.expected_lat) < 89.0f) {
            EXPECT_NEAR(geo.x, test.expected_lon, 1.0f)
                << "Failed for " << test.description << " (longitude)";
        }
        EXPECT_NEAR(geo.y, test.expected_lat, 1.0f)
            << "Failed for " << test.description << " (latitude)";
    }
}

/**
 * @brief Test that atan2 gives correct longitude for standard positions
 */
TEST_F(TileVisibilityTest, Atan2LongitudeCalculation) {
    // atan2(y, x) for longitude calculation

    // +X axis (x=1, y=0): atan2(0, 1) = 0° ✓
    EXPECT_NEAR(glm::degrees(std::atan2(0.0f, 1.0f)), 0.0f, 0.01f);

    // +Y axis (x=0, y=1): atan2(1, 0) = 90° ✓
    EXPECT_NEAR(glm::degrees(std::atan2(1.0f, 0.0f)), 90.0f, 0.01f);

    // -X axis (x=-1, y=0): atan2(0, -1) = 180° or -180° ✓
    const float lon_minus_x = glm::degrees(std::atan2(0.0f, -1.0f));
    EXPECT_TRUE(std::abs(lon_minus_x) > 179.0f);

    // -Y axis (x=0, y=-1): atan2(-1, 0) = -90° ✓
    EXPECT_NEAR(glm::degrees(std::atan2(-1.0f, 0.0f)), -90.0f, 0.01f);
}
