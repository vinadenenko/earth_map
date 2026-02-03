#include <earth_map/earth_map.h>
#include <earth_map/core/camera_controller.h>
#include <earth_map/constants.h>
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
        config_.enable_opengl_debug = false;

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
 * @brief Test that camera starts in normalized coordinate system
 */
TEST_F(CameraInputIntegrationTest, InitialCameraPositionNormalized) {
    // Camera should start at DEFAULT_CAMERA_DISTANCE_NORMALIZED (2.5)
    glm::vec3 pos = camera_controller_->GetPosition();
    float distance = glm::length(pos);

    // Should be around 2.5 (DEFAULT_CAMERA_DISTANCE_NORMALIZED)
    // and within the valid constraint range
    EXPECT_NEAR(distance, constants::camera::DEFAULT_CAMERA_DISTANCE_NORMALIZED, 0.1f);
    EXPECT_LE(distance, constants::camera_constraints::MAX_DISTANCE_NORMALIZED);

    // Should NOT be in meters (would be ~19 million)
    EXPECT_LT(distance, 100.0f);
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

    glm::vec3 initial_pos = camera_controller_->GetPosition();

    // Simulate mouse press
    InputEvent press_event;
    press_event.type = InputEvent::Type::MOUSE_BUTTON_PRESS;
    press_event.button = 0;
    press_event.x = 960.0;  // Center of 1920 width
    press_event.y = 540.0;  // Center of 1080 height
    press_event.timestamp = 1000;
    camera_controller_->ProcessInput(press_event);

    // Simulate mouse drag (100 pixels to the right)
    InputEvent move_event;
    move_event.type = InputEvent::Type::MOUSE_MOVE;
    move_event.x = 1060.0;
    move_event.y = 540.0;
    move_event.timestamp = 1100;
    camera_controller_->ProcessInput(move_event);

    glm::vec3 new_pos = camera_controller_->GetPosition();

    // Position should have changed (camera rotated around target)
    EXPECT_GT(glm::distance(initial_pos, new_pos), 0.01f);

    // Distance from origin should remain the same (orbital rotation)
    EXPECT_NEAR(glm::length(initial_pos), glm::length(new_pos), 0.01f);

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
 * @brief Test mouse scroll zooms in normalized coordinate system
 */
TEST_F(CameraInputIntegrationTest, MouseScrollZooms) {
    glm::vec3 initial_pos = camera_controller_->GetPosition();
    float initial_distance = glm::length(initial_pos);

    // Scroll up (zoom in)
    InputEvent scroll_event;
    scroll_event.type = InputEvent::Type::MOUSE_SCROLL;
    scroll_event.scroll_delta = 1.0f;  // Positive = zoom in
    scroll_event.timestamp = 1000;

    bool handled = camera_controller_->ProcessInput(scroll_event);
    EXPECT_TRUE(handled);

    glm::vec3 new_pos = camera_controller_->GetPosition();
    float new_distance = glm::length(new_pos);

    // Distance should have decreased (zoomed in)
    EXPECT_LT(new_distance, initial_distance);

    // Should still be in normalized range (not jump to meters)
    EXPECT_LT(new_distance, 100.0f);
    EXPECT_GT(new_distance, 0.1f);
}

/**
 * @brief Test zoom respects normalized coordinate constraints
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

    glm::vec3 pos = camera_controller_->GetPosition();
    float distance = glm::length(pos);

    // Should be clamped to minimum distance (not go below globe surface)
    EXPECT_GE(distance, constants::camera_constraints::MIN_DISTANCE_NORMALIZED);

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

    pos = camera_controller_->GetPosition();
    distance = glm::length(pos);

    // Should be clamped to maximum distance
    EXPECT_LE(distance, constants::camera_constraints::MAX_DISTANCE_NORMALIZED);
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
    glm::vec3 target = camera_controller_->GetTarget();

    // Target should be at origin (0,0,0) for globe center
    EXPECT_NEAR(target.x, 0.0f, 0.01f);
    EXPECT_NEAR(target.y, 0.0f, 0.01f);
    EXPECT_NEAR(target.z, 0.0f, 0.01f);

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
    move_event.timestamp = 1100;
    camera_controller_->ProcessInput(move_event);

    // Target should still be at origin
    target = camera_controller_->GetTarget();
    EXPECT_NEAR(target.x, 0.0f, 0.01f);
    EXPECT_NEAR(target.y, 0.0f, 0.01f);
    EXPECT_NEAR(target.z, 0.0f, 0.01f);
}
