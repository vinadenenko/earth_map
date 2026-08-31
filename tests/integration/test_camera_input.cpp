#include <earth_map/earth_map.h>
#include <earth_map/core/camera_controller.h>
#include <earth_map/constants.h>
#include <earth_map/geodesy/wgs84_ellipsoid.h>
#include <earth_map/renderer/camera.h>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>

using namespace earth_map;

/**
 * @brief Integration test for camera input handling with normalized coordinates
 */
class CameraInputIntegrationTest : public ::testing::Test {
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

    Configuration config_;
    CameraController* camera_controller_ = nullptr;
};

/**
 * @brief Test that camera starts above the WGS84 surface in real metres
 */
TEST_F(CameraInputIntegrationTest, InitialCameraPositionIsAboveSurfaceInRealMetres) {
    const auto geodetic =
        geodesy::Wgs84Ellipsoid::FromEcef(camera_controller_->GetEcefPosition());
    ASSERT_TRUE(geodetic.has_value());

    // Camera must start well above the WGS84 surface in real metres -- not
    // at the old normalized-sphere scale (which would be a value near 1-3).
    EXPECT_GT(geodetic->ellipsoid_height_meters, 1000.0);
    EXPECT_LT(geodetic->ellipsoid_height_meters, 20'000'000.0);
}

/**
 * @brief Test mouse button press/release events
 */
TEST_F(CameraInputIntegrationTest, MouseButtonEvents) {
    // Create mouse press event
    InputEvent press_event;
    press_event.type = InputEvent::Type::MOUSE_BUTTON_PRESS;
    press_event.button = 0;  // Left button
    press_event.x = 100.0;
    press_event.y = 100.0;
    press_event.timestamp = 1000;

    bool handled = camera_controller_->ProcessInput(press_event);
    EXPECT_TRUE(handled);

    // Create mouse release event
    InputEvent release_event;
    release_event.type = InputEvent::Type::MOUSE_BUTTON_RELEASE;
    release_event.button = 0;
    release_event.x = 100.0;
    release_event.y = 100.0;
    release_event.timestamp = 2000;

    handled = camera_controller_->ProcessInput(release_event);
    EXPECT_TRUE(handled);
}

/**
 * @brief Test mouse drag rotates camera in ORBIT mode
 */
TEST_F(CameraInputIntegrationTest, MouseDragRotatesCamera) {
    // Ensure we're in ORBIT mode
    ASSERT_EQ(camera_controller_->GetMovementMode(), CameraController::MovementMode::ORBIT);

    const geodesy::EcefPosition initial_position = camera_controller_->GetEcefPosition();

    // Simulate mouse press
    InputEvent press_event;
    press_event.type = InputEvent::Type::MOUSE_BUTTON_PRESS;
    press_event.button = 0;
    press_event.x = 960.0;  // Center of 1920 width
    press_event.y = 540.0;  // Center of 1080 height
    press_event.timestamp = 1000;
    camera_controller_->ProcessInput(press_event);

    // Simulate mouse drag (100 pixels to the right). ProcessInput's
    // MOUSE_MOVE handling reads the dx/dy delta fields directly, not x/y
    // absolute positions.
    InputEvent move_event;
    move_event.type = InputEvent::Type::MOUSE_MOVE;
    move_event.x = 1060.0;
    move_event.y = 540.0;
    move_event.dx = 100.0f;
    move_event.dy = 0.0f;
    move_event.timestamp = 1100;
    camera_controller_->ProcessInput(move_event);

    const geodesy::EcefPosition new_position = camera_controller_->GetEcefPosition();

    // Position should have changed (camera rotated around target)
    EXPECT_GT(glm::distance(initial_position.meters, new_position.meters), 0.01);

    // Distance from origin should remain the same (orbital rotation)
    EXPECT_NEAR(glm::length(initial_position.meters), glm::length(new_position.meters), 1.0);

    // Simulate mouse release
    InputEvent release_event;
    release_event.type = InputEvent::Type::MOUSE_BUTTON_RELEASE;
    release_event.button = 0;
    release_event.x = 1060.0;
    release_event.y = 540.0;
    release_event.timestamp = 1200;
    camera_controller_->ProcessInput(release_event);
}

/**
 * @brief Test mouse scroll zooms, in real metres
 */
TEST_F(CameraInputIntegrationTest, MouseScrollZooms) {
    const double initial_distance = glm::length(camera_controller_->GetEcefPosition().meters);

    // Scroll up (zoom in)
    InputEvent scroll_event;
    scroll_event.type = InputEvent::Type::MOUSE_SCROLL;
    scroll_event.scroll_delta = 1.0f;  // Positive = zoom in
    scroll_event.timestamp = 1000;

    bool handled = camera_controller_->ProcessInput(scroll_event);
    EXPECT_TRUE(handled);

    const double new_distance = glm::length(camera_controller_->GetEcefPosition().meters);

    // Distance should have decreased (zoomed in)
    EXPECT_LT(new_distance, initial_distance);

    // Should stay a physically sane distance from Earth's centre.
    EXPECT_GT(new_distance, geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters);
}

/**
 * @brief Test zoom respects the default altitude constraints, in real metres
 */
TEST_F(CameraInputIntegrationTest, ZoomConstraints) {
    // Try to zoom in very far
    for (int i = 0; i < 100; ++i) {
        InputEvent scroll_event;
        scroll_event.type = InputEvent::Type::MOUSE_SCROLL;
        scroll_event.scroll_delta = 1.0f;
        scroll_event.timestamp = 1000 + i * 10;
        camera_controller_->ProcessInput(scroll_event);
    }

    auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(camera_controller_->GetEcefPosition());
    ASSERT_TRUE(geodetic.has_value());

    // Should be clamped to the default minimum altitude (not go below the
    // globe surface). CameraConstraints::min_altitude defaults to 100m.
    EXPECT_GE(geodetic->ellipsoid_height_meters, 100.0 - 1e-6);

    // Reset camera
    camera_controller_->Reset();

    // Try to zoom out very far
    for (int i = 0; i < 100; ++i) {
        InputEvent scroll_event;
        scroll_event.type = InputEvent::Type::MOUSE_SCROLL;
        scroll_event.scroll_delta = -1.0f;
        scroll_event.timestamp = 2000 + i * 10;
        camera_controller_->ProcessInput(scroll_event);
    }

    geodetic = geodesy::Wgs84Ellipsoid::FromEcef(camera_controller_->GetEcefPosition());
    ASSERT_TRUE(geodetic.has_value());

    // Should be clamped to the default maximum altitude (10,000km).
    EXPECT_LE(geodetic->ellipsoid_height_meters, 10'000'000.0 + 1e-6);
}

/**
 * @brief Test camera maintains ORBIT mode during input
 */
TEST_F(CameraInputIntegrationTest, MaintainsOrbitMode) {
    EXPECT_EQ(camera_controller_->GetMovementMode(), CameraController::MovementMode::ORBIT);

    // Process various inputs
    InputEvent press_event;
    press_event.type = InputEvent::Type::MOUSE_BUTTON_PRESS;
    press_event.button = 0;
    press_event.x = 100.0;
    press_event.y = 100.0;
    press_event.timestamp = 1000;
    camera_controller_->ProcessInput(press_event);

    // Should still be in ORBIT mode
    EXPECT_EQ(camera_controller_->GetMovementMode(), CameraController::MovementMode::ORBIT);

    InputEvent scroll_event;
    scroll_event.type = InputEvent::Type::MOUSE_SCROLL;
    scroll_event.scroll_delta = 1.0f;
    scroll_event.timestamp = 2000;
    camera_controller_->ProcessInput(scroll_event);

    // Should still be in ORBIT mode
    EXPECT_EQ(camera_controller_->GetMovementMode(), CameraController::MovementMode::ORBIT);
}

/**
 * @brief Test target remains fixed at origin in ORBIT mode
 */
TEST_F(CameraInputIntegrationTest, TargetRemainsAtOrigin) {
    geodesy::EcefPosition target = camera_controller_->GetEcefTarget();

    // Target should be at ECEF origin (0,0,0) for globe center
    EXPECT_NEAR(target.meters.x, 0.0, 0.01);
    EXPECT_NEAR(target.meters.y, 0.0, 0.01);
    EXPECT_NEAR(target.meters.z, 0.0, 0.01);

    // Rotate camera
    InputEvent press_event;
    press_event.type = InputEvent::Type::MOUSE_BUTTON_PRESS;
    press_event.button = 0;
    press_event.x = 100.0;
    press_event.y = 100.0;
    press_event.timestamp = 1000;
    camera_controller_->ProcessInput(press_event);

    InputEvent move_event;
    move_event.type = InputEvent::Type::MOUSE_MOVE;
    move_event.x = 200.0;
    move_event.y = 100.0;
    move_event.dx = 100.0f;
    move_event.dy = 0.0f;
    move_event.timestamp = 1100;
    camera_controller_->ProcessInput(move_event);

    // Target should still be at origin
    target = camera_controller_->GetEcefTarget();
    EXPECT_NEAR(target.meters.x, 0.0, 0.01);
    EXPECT_NEAR(target.meters.y, 0.0, 0.01);
    EXPECT_NEAR(target.meters.z, 0.0, 0.01);
}
