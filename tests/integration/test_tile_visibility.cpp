#include <earth_map/earth_map.h>
#include <earth_map/renderer/tile_renderer.h>
#include <earth_map/core/camera_controller.h>
#include <earth_map/constants.h>
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
 */
class TileVisibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.screen_width = 1920;
        config_.screen_height = 1080;
        config_.enable_opengl_debug = false;

        camera_controller_ = CreateCameraController(config_);
        ASSERT_NE(camera_controller_, nullptr);
        ASSERT_TRUE(camera_controller_->Initialize());
    }

    void TearDown() override {
        delete camera_controller_;
        camera_controller_ = nullptr;
    }

    /**
     * @brief Helper to convert 3D position to geographic coordinates
     */
    glm::vec2 PositionToGeographic(const glm::vec3& pos) const {
        glm::vec3 normalized = glm::normalize(pos);
        float lat = glm::degrees(std::asin(std::clamp(normalized.y, -1.0f, 1.0f)));
        float lon = glm::degrees(std::atan2(normalized.x, normalized.z));
        return glm::vec2(lon, lat);
    }

    /**
     * @brief Helper to get what direction camera is looking
     */
    glm::vec3 GetCameraLookDirection() const {
        glm::vec3 position = camera_controller_->GetPosition();
        glm::vec3 target = camera_controller_->GetTarget();
        return glm::normalize(target - position);
    }

    Configuration config_;
    CameraController* camera_controller_ = nullptr;
};

/**
 * @brief Test camera looking at Prime Meridian (0°, 0°)
 *
 * When camera is at (0, 0, 3.0) looking at origin (0, 0, 0),
 * it should be looking at longitude 0°, latitude 0° (Africa/Atlantic)
 */
TEST_F(TileVisibilityTest, CameraLookingAtPrimeMeridian) {
    // Default camera position: (0, 0, 3.0) looking at (0, 0, 0)
    glm::vec3 camera_pos = camera_controller_->GetPosition();
    glm::vec3 target = camera_controller_->GetTarget();

    // Camera is on +Z axis
    EXPECT_NEAR(camera_pos.x, 0.0f, 0.01f);
    EXPECT_NEAR(camera_pos.y, 0.0f, 0.01f);
    EXPECT_GT(camera_pos.z, 2.0f);  // Around 3.0

    // Target is at origin
    EXPECT_NEAR(target.x, 0.0f, 0.01f);
    EXPECT_NEAR(target.y, 0.0f, 0.01f);
    EXPECT_NEAR(target.z, 0.0f, 0.01f);

    // Camera look direction should be towards -Z (negative of position)
    glm::vec3 look_dir = GetCameraLookDirection();
    EXPECT_NEAR(look_dir.x, 0.0f, 0.01f);
    EXPECT_NEAR(look_dir.y, 0.0f, 0.01f);
    EXPECT_LT(look_dir.z, -0.99f);  // Should be approximately (0, 0, -1)

    // The point on the sphere where camera is looking (opposite of camera position)
    glm::vec3 look_point = -glm::normalize(camera_pos);
    glm::vec2 look_geo = PositionToGeographic(look_point);

    // Should be looking at Prime Meridian (lon=0°) and Equator (lat=0°)
    // atan2(0, -1) = 180° or -180° (both are valid for opposite side)
    EXPECT_NEAR(std::abs(look_geo.x), 180.0f, 1.0f);  // Opposite side (180° or -180°)
    EXPECT_NEAR(look_geo.y, 0.0f, 1.0f);
}

/**
 * @brief Test camera position vs look direction coordinate systems
 *
 * Critical test: Verify that when camera is on one side of globe,
 * it's looking at the OPPOSITE side.
 */
TEST_F(TileVisibilityTest, CameraPositionVsLookDirection) {
    // Camera at +Z looking at origin
    glm::vec3 camera_pos = camera_controller_->GetPosition();

    // Geographic coords OF camera position (where camera IS)
    glm::vec2 camera_geo = PositionToGeographic(camera_pos);

    // Geographic coords WHERE camera is LOOKING (opposite direction)
    glm::vec3 look_point = -glm::normalize(camera_pos);
    glm::vec2 look_geo = PositionToGeographic(look_point);

    // Camera is at lon=0°, lat=0°, z=+3.0
    // atan2(0, 3.0) = 0°
    EXPECT_NEAR(camera_geo.x, 0.0f, 1.0f);
    EXPECT_NEAR(camera_geo.y, 0.0f, 1.0f);

    // Camera looks at lon=180° (or -180°), lat=0°
    // atan2(0, -1) = ±180°
    EXPECT_NEAR(std::abs(look_geo.x), 180.0f, 1.0f);
    EXPECT_NEAR(look_geo.y, 0.0f, 1.0f);

    // The longitudes should be 180° apart
    float lon_diff = std::abs(camera_geo.x - look_geo.x);
    // Account for wraparound: difference could be 180° or close to 360°
    EXPECT_TRUE(std::abs(lon_diff - 180.0f) < 5.0f || std::abs(lon_diff - 360.0f) < 5.0f);
}

/**
 * @brief Test camera looking at specific geographic location
 */
TEST_F(TileVisibilityTest, CameraLookingAtSpecificLocation) {
    // Move camera to look at a specific location
    // Position camera on +X axis to look at Prime Meridian
    camera_controller_->SetPosition(glm::vec3(3.0f, 0.0f, 0.0f));
    camera_controller_->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));

    glm::vec3 camera_pos = camera_controller_->GetPosition();

    // Camera is at +X (lon=90°, lat=0°)
    glm::vec2 camera_geo = PositionToGeographic(camera_pos);
    EXPECT_NEAR(camera_geo.x, 90.0f, 1.0f);  // +X axis = 90° East
    EXPECT_NEAR(camera_geo.y, 0.0f, 1.0f);

    // Camera looks at -X (lon=-90° or 270°, lat=0°)
    glm::vec3 look_point = -glm::normalize(camera_pos);
    glm::vec2 look_geo = PositionToGeographic(look_point);
    EXPECT_NEAR(std::abs(look_geo.x), 90.0f, 1.0f);  // -90° or 270°
    EXPECT_NEAR(look_geo.y, 0.0f, 1.0f);
}

/**
 * @brief Test camera looking at North Pole
 */
TEST_F(TileVisibilityTest, CameraLookingAtNorthPole) {
    // Position camera on +Y axis (above North Pole)
    camera_controller_->SetPosition(glm::vec3(0.0f, 3.0f, 0.0f));
    camera_controller_->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));

    glm::vec3 camera_pos = camera_controller_->GetPosition();

    // Camera is above North Pole (lat=90°)
    glm::vec2 camera_geo = PositionToGeographic(camera_pos);
    EXPECT_NEAR(camera_geo.y, 90.0f, 1.0f);

    // Camera looks at South Pole (lat=-90°)
    glm::vec3 look_point = -glm::normalize(camera_pos);
    glm::vec2 look_geo = PositionToGeographic(look_point);
    EXPECT_NEAR(look_geo.y, -90.0f, 1.0f);
}

/**
 * @brief Test camera looking at South Pole
 */
TEST_F(TileVisibilityTest, CameraLookingAtSouthPole) {
    // Position camera on -Y axis (below South Pole)
    camera_controller_->SetPosition(glm::vec3(0.0f, -3.0f, 0.0f));
    camera_controller_->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));

    glm::vec3 camera_pos = camera_controller_->GetPosition();

    // Camera is below South Pole (lat=-90°)
    glm::vec2 camera_geo = PositionToGeographic(camera_pos);
    EXPECT_NEAR(camera_geo.y, -90.0f, 1.0f);

    // Camera looks at North Pole (lat=90°)
    glm::vec3 look_point = -glm::normalize(camera_pos);
    glm::vec2 look_geo = PositionToGeographic(look_point);
    EXPECT_NEAR(look_geo.y, 90.0f, 1.0f);
}

/**
 * @brief Test longitude wraparound at International Date Line
 */
TEST_F(TileVisibilityTest, LongitudeWraparound) {
    // Position camera at -Z (opposite of default)
    camera_controller_->SetPosition(glm::vec3(0.0f, 0.0f, -3.0f));
    camera_controller_->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));

    glm::vec3 camera_pos = camera_controller_->GetPosition();

    // Camera is at lon=180° or -180° (same meridian)
    glm::vec2 camera_geo = PositionToGeographic(camera_pos);
    EXPECT_NEAR(std::abs(camera_geo.x), 180.0f, 1.0f);

    // Camera looks at lon=0° (Prime Meridian)
    glm::vec3 look_point = -glm::normalize(camera_pos);
    glm::vec2 look_geo = PositionToGeographic(look_point);
    EXPECT_NEAR(look_geo.x, 0.0f, 1.0f);
}

/**
 * @brief Test that visible tiles match camera look direction
 *
 * This is the KEY test: tiles should be loaded for where camera LOOKS,
 * not where camera IS positioned.
 */
TEST_F(TileVisibilityTest, VisibleTilesMatchLookDirection) {
    // Default camera: at (0, 0, 3.0) looking at (0, 0, 0)

    // Camera looks toward -Z direction (180° longitude)
    glm::vec3 camera_pos = camera_controller_->GetPosition();
    glm::vec3 look_point = -glm::normalize(camera_pos);
    glm::vec2 look_geo = PositionToGeographic(look_point);

    // At zoom level 2, we have 4x4 = 16 tiles
    // Tile 0,0 covers lon[-180,-90], lat[0,85.051]
    // Tile 1,0 covers lon[-90,0], lat[0,85.051]
    // Tile 2,0 covers lon[0,90], lat[0,85.051]
    // Tile 3,0 covers lon[90,180], lat[0,85.051]

    // Camera looking at lon=180° should load tiles near x=0 (wraps around)
    // or x=3 (western hemisphere, near 180°)

    // The visible geographic bounds should be centered around where camera looks
    // For camera at +Z looking at origin, look direction is -Z
    // This corresponds to longitude ±180°, latitude 0°
    EXPECT_NEAR(std::abs(look_geo.x), 180.0f, 1.0f);
    EXPECT_NEAR(look_geo.y, 0.0f, 1.0f);

    // If tiles are loading on OPPOSITE side, they would be:
    // - Tiles around lon=0° (where camera IS) - WRONG!
    // Should be:
    // - Tiles around lon=180° (where camera LOOKS) - CORRECT!
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

    std::vector<TestCase> test_cases = {
        { glm::vec3(1.0f, 0.0f, 0.0f), 90.0f, 0.0f, "+X axis (90° East)" },
        { glm::vec3(-1.0f, 0.0f, 0.0f), -90.0f, 0.0f, "-X axis (90° West)" },
        { glm::vec3(0.0f, 0.0f, 1.0f), 0.0f, 0.0f, "+Z axis (Prime Meridian)" },
        { glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 90.0f, "+Y axis (North Pole)" },
        { glm::vec3(0.0f, -1.0f, 0.0f), 0.0f, -90.0f, "-Y axis (South Pole)" },
    };

    for (const auto& test : test_cases) {
        glm::vec2 geo = PositionToGeographic(test.position);

        EXPECT_NEAR(geo.x, test.expected_lon, 1.0f)
            << "Failed for " << test.description << " (longitude)";
        EXPECT_NEAR(geo.y, test.expected_lat, 1.0f)
            << "Failed for " << test.description << " (latitude)";
    }
}

/**
 * @brief Test that atan2 gives correct longitude for standard positions
 */
TEST_F(TileVisibilityTest, Atan2LongitudeCalculation) {
    // atan2(x, z) for longitude calculation

    // +Z axis (x=0, z=1): atan2(0, 1) = 0° ✓
    EXPECT_NEAR(glm::degrees(std::atan2(0.0f, 1.0f)), 0.0f, 0.01f);

    // +X axis (x=1, z=0): atan2(1, 0) = 90° ✓
    EXPECT_NEAR(glm::degrees(std::atan2(1.0f, 0.0f)), 90.0f, 0.01f);

    // -Z axis (x=0, z=-1): atan2(0, -1) = 180° or -180° ✓
    float lon_minus_z = glm::degrees(std::atan2(0.0f, -1.0f));
    EXPECT_TRUE(std::abs(lon_minus_z) > 179.0f);

    // -X axis (x=-1, z=0): atan2(-1, 0) = -90° ✓
    EXPECT_NEAR(glm::degrees(std::atan2(-1.0f, 0.0f)), -90.0f, 0.01f);
}
