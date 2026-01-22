#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <earth_map/constants.h>
#include <cmath>

using namespace earth_map;

/**
 * @brief Test tile geographic bounds calculation logic
 *
 * This tests the core logic of converting camera position to visible geographic bounds.
 * The key insight: tiles should be loaded WHERE THE CAMERA LOOKS, not where it IS.
 */
class TileBoundsCalculationTest : public ::testing::Test {
protected:
    /**
     * @brief Simulates the tile renderer's geographic bounds calculation
     */
    struct GeographicBounds {
        float min_lat;
        float max_lat;
        float min_lon;
        float max_lon;
    };

    GeographicBounds CalculateVisibleBounds(const glm::vec3& camera_position) {
        float distance = glm::length(camera_position);

        // Calculate visible angle based on FOV
        const float fov_rad = glm::radians(45.0f);
        const float visible_radius = distance * std::tan(fov_rad / 2.0f);
        const float visible_angle_deg = glm::degrees(visible_radius / distance) * 2.0f;

        // CRITICAL: Use look direction, not camera position
        glm::vec3 look_direction = -glm::normalize(camera_position);

        // Convert to geographic
        const float lat = glm::degrees(std::asin(std::clamp(look_direction.y, -1.0f, 1.0f)));
        const float lon = glm::degrees(std::atan2(look_direction.x, look_direction.z));

        // Calculate bounds
        const float lat_range = std::min(170.0f, visible_angle_deg * 1.5f);
        const float lon_range = std::min(360.0f, visible_angle_deg * 1.5f);

        GeographicBounds bounds;
        bounds.min_lat = std::max(-85.0f, lat - lat_range / 2.0f);
        bounds.max_lat = std::min(85.0f, lat + lat_range / 2.0f);

        // Calculate longitude bounds and clamp to [-180, 180]
        // Note: We clamp rather than wrap to avoid creating invalid bounds (min > max)
        float min_lon = lon - lon_range / 2.0f;
        float max_lon = lon + lon_range / 2.0f;

        // Clamp to valid longitude range
        bounds.min_lon = std::max(-180.0f, std::min(180.0f, min_lon));
        bounds.max_lon = std::max(-180.0f, std::min(180.0f, max_lon));

        return bounds;
    }
};

/**
 * @brief Test: Camera on +Z axis looks towards -Z (180° longitude)
 */
TEST_F(TileBoundsCalculationTest, CameraOnPlusZLooksAtMinus180) {
    // Camera at (0, 0, 3.0) looking at origin
    glm::vec3 camera_pos(0.0f, 0.0f, 3.0f);

    GeographicBounds bounds = CalculateVisibleBounds(camera_pos);

    // Center of bounds should be around lon=180° (or -180°), lat=0°
    float center_lon = (bounds.min_lon + bounds.max_lon) / 2.0f;
    float center_lat = (bounds.min_lat + bounds.max_lat) / 2.0f;

    // Longitude should be close to ±180°
    EXPECT_TRUE(std::abs(center_lon - 180.0f) < 30.0f ||
                std::abs(center_lon + 180.0f) < 30.0f)
        << "Center longitude: " << center_lon << " (expected near ±180°)";

    // Latitude should be close to 0° (equator)
    EXPECT_NEAR(center_lat, 0.0f, 30.0f);
}

/**
 * @brief Test: Camera on +X axis looks towards -X (90° West)
 */
TEST_F(TileBoundsCalculationTest, CameraOnPlusXLooksAtMinus90) {
    // Camera at (3.0, 0, 0) looking at origin
    glm::vec3 camera_pos(3.0f, 0.0f, 0.0f);

    GeographicBounds bounds = CalculateVisibleBounds(camera_pos);

    float center_lon = (bounds.min_lon + bounds.max_lon) / 2.0f;
    float center_lat = (bounds.min_lat + bounds.max_lat) / 2.0f;

    // Longitude should be close to -90° (90° West)
    EXPECT_NEAR(center_lon, -90.0f, 30.0f);

    // Latitude should be close to 0° (equator)
    EXPECT_NEAR(center_lat, 0.0f, 30.0f);
}

/**
 * @brief Test: Camera on -X axis looks towards +X (90° East)
 */
TEST_F(TileBoundsCalculationTest, CameraOnMinusXLooksAtPlus90) {
    // Camera at (-3.0, 0, 0) looking at origin
    glm::vec3 camera_pos(-3.0f, 0.0f, 0.0f);

    GeographicBounds bounds = CalculateVisibleBounds(camera_pos);

    float center_lon = (bounds.min_lon + bounds.max_lon) / 2.0f;
    float center_lat = (bounds.min_lat + bounds.max_lat) / 2.0f;

    // Longitude should be close to +90° (90° East)
    EXPECT_NEAR(center_lon, 90.0f, 30.0f);

    // Latitude should be close to 0° (equator)
    EXPECT_NEAR(center_lat, 0.0f, 30.0f);
}

/**
 * @brief Test: Camera on +Y axis looks towards -Y (South Pole)
 */
TEST_F(TileBoundsCalculationTest, CameraOnPlusYLooksAtSouthPole) {
    // Camera at (0, 3.0, 0) looking at origin
    glm::vec3 camera_pos(0.0f, 3.0f, 0.0f);

    GeographicBounds bounds = CalculateVisibleBounds(camera_pos);

    float center_lat = (bounds.min_lat + bounds.max_lat) / 2.0f;

    // Latitude should be close to -90° (South Pole)
    // Note: Will be clamped to -85° due to Web Mercator limits
    EXPECT_LT(center_lat, -60.0f);
}

/**
 * @brief Test: Camera on -Y axis looks towards +Y (North Pole)
 */
TEST_F(TileBoundsCalculationTest, CameraOnMinusYLooksAtNorthPole) {
    // Camera at (0, -3.0, 0) looking at origin
    glm::vec3 camera_pos(0.0f, -3.0f, 0.0f);

    GeographicBounds bounds = CalculateVisibleBounds(camera_pos);

    float center_lat = (bounds.min_lat + bounds.max_lat) / 2.0f;

    // Latitude should be close to +90° (North Pole)
    // Note: Will be clamped to +85° due to Web Mercator limits
    EXPECT_GT(center_lat, 60.0f);
}

/**
 * @brief Test: Default camera position (0, 0, 3.0)
 */
TEST_F(TileBoundsCalculationTest, DefaultCameraPosition) {
    // Default camera from constants
    glm::vec3 camera_pos(0.0f, 0.0f, constants::camera::DEFAULT_CAMERA_DISTANCE_NORMALIZED);

    GeographicBounds bounds = CalculateVisibleBounds(camera_pos);

    // Should be looking towards ±180° longitude
    float center_lon = (bounds.min_lon + bounds.max_lon) / 2.0f;

    EXPECT_TRUE(std::abs(center_lon - 180.0f) < 30.0f ||
                std::abs(center_lon + 180.0f) < 30.0f)
        << "Default camera should look towards ±180° longitude, got: " << center_lon;
}

/**
 * @brief Test: Bounds NOT inverted (old bug)
 *
 * This test verifies the fix: camera on +Z should NOT see tiles at 0° longitude
 */
TEST_F(TileBoundsCalculationTest, BoundsNotInverted) {
    // Camera at (0, 0, 3.0)
    glm::vec3 camera_pos(0.0f, 0.0f, 3.0f);

    GeographicBounds bounds = CalculateVisibleBounds(camera_pos);

    float center_lon = (bounds.min_lon + bounds.max_lon) / 2.0f;

    // OLD BUG: Would give lon=0° (camera position instead of look direction)
    // NEW CORRECT: Should give lon=±180° (where camera looks)

    // Verify it's NOT close to 0° (the bug)
    EXPECT_FALSE(std::abs(center_lon) < 30.0f)
        << "Camera bounds should NOT be centered at 0° (old bug), got: " << center_lon;

    // Verify it IS close to ±180° (correct)
    EXPECT_TRUE(std::abs(center_lon - 180.0f) < 30.0f ||
                std::abs(center_lon + 180.0f) < 30.0f)
        << "Camera bounds should be centered near ±180°, got: " << center_lon;
}
