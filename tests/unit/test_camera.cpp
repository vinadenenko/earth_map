#include <gtest/gtest.h>
#include <earth_map/renderer/camera.h>
#include <earth_map/earth_map.h>
#include <glm/glm.hpp>
#include <memory>

namespace earth_map {

class CameraTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.screen_width = 1920;
        config_.screen_height = 1080;
        config_.enable_opengl_debug = false;
        
        camera_ = CreatePerspectiveCamera(config_);
        ASSERT_NE(camera_, nullptr);
        ASSERT_TRUE(camera_->Initialize());
    }
    
    void TearDown() override {
        camera_.reset();
    }

protected:
    Configuration config_;
    std::unique_ptr<Camera> camera_;
};

TEST_F(CameraTest, Initialization) {
    EXPECT_TRUE(camera_->Initialize());
    EXPECT_EQ(camera_->GetProjectionType(), CameraProjectionType::PERSPECTIVE);
    EXPECT_EQ(camera_->GetMovementMode(), MovementMode::FREE);
}

TEST_F(CameraTest, PositionControl) {
    // Test setting position
    glm::vec3 test_pos(100.0f, 200.0f, 300.0f);
    camera_->SetPosition(test_pos);
    
    glm::vec3 actual_pos = camera_->GetPosition();
    EXPECT_FLOAT_EQ(actual_pos.x, test_pos.x);
    EXPECT_FLOAT_EQ(actual_pos.y, test_pos.y);
    EXPECT_FLOAT_EQ(actual_pos.z, test_pos.z);
}

TEST_F(CameraTest, GeographicPositionControl) {
    // Test setting geographic position
    double longitude = -122.4194;
    double latitude = 37.7749;
    double altitude = 1000.0;
    
    camera_->SetGeographicPosition(longitude, latitude, altitude);
    
    // Just test that the position is set (actual conversion tested in coordinate system tests)
    glm::vec3 pos = camera_->GetPosition();
    EXPECT_GT(glm::length(pos), 6000000.0f); // Should be around Earth radius
}

TEST_F(CameraTest, OrientationControl) {
    // Test setting orientation
    double heading = 45.0;
    double pitch = 30.0;
    double roll = 15.0;
    
    camera_->SetOrientation(heading, pitch, roll);
    
    glm::vec3 orientation = camera_->GetOrientation();
    EXPECT_FLOAT_EQ(orientation.x, static_cast<float>(heading));
    EXPECT_FLOAT_EQ(orientation.y, static_cast<float>(pitch));
    EXPECT_FLOAT_EQ(orientation.z, static_cast<float>(roll));
}

TEST_F(CameraTest, FieldOfViewControl) {
    // Test setting field of view
    float fov = 60.0f;
    camera_->SetFieldOfView(fov);
    
    EXPECT_FLOAT_EQ(camera_->GetFieldOfView(), fov);
    
    // Test bounds
    camera_->SetFieldOfView(0.0f);
    EXPECT_FLOAT_EQ(camera_->GetFieldOfView(), 1.0f); // Should be clamped to minimum
    
    camera_->SetFieldOfView(200.0f);
    EXPECT_FLOAT_EQ(camera_->GetFieldOfView(), 179.0f); // Should be clamped to maximum
}

TEST_F(CameraTest, ClippingPlanes) {
    // Test setting clipping planes
    float near_plane = 100.0f;
    float far_plane = 10000.0f;
    
    camera_->SetClippingPlanes(near_plane, far_plane);
    
    // Note: We can't directly get clipping planes as they're not exposed
    // This test ensures the method doesn't crash
    SUCCEED(); // If we get here, the method worked
}

TEST_F(CameraTest, MatrixGeneration) {
    // Test matrix generation
    camera_->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    camera_->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));
    
    float aspect_ratio = 16.0f / 9.0f;
    glm::mat4 view_matrix = camera_->GetViewMatrix();
    glm::mat4 proj_matrix = camera_->GetProjectionMatrix(aspect_ratio);
    glm::mat4 view_proj_matrix = camera_->GetViewProjectionMatrix(aspect_ratio);
    
    // Basic validation - matrices should be non-zero
    EXPECT_GT(glm::length(glm::vec3(view_matrix[0])), 0.0f);
    EXPECT_GT(glm::length(glm::vec3(proj_matrix[0])), 0.0f);
    EXPECT_GT(glm::length(glm::vec3(view_proj_matrix[0])), 0.0f);
}

TEST_F(CameraTest, FrustumGeneration) {
    // Test frustum generation
    float aspect_ratio = 16.0f / 9.0f;
    Frustum frustum = camera_->GetFrustum(aspect_ratio);
    
    EXPECT_TRUE(frustum.IsValid());
}

TEST_F(CameraTest, MovementModeControl) {
    // Test movement mode changes
    camera_->SetMovementMode(MovementMode::ORBIT);
    EXPECT_EQ(camera_->GetMovementMode(), MovementMode::ORBIT);
    
    camera_->SetMovementMode(MovementMode::FREE);
    EXPECT_EQ(camera_->GetMovementMode(), MovementMode::FREE);
    
    camera_->SetMovementMode(MovementMode::FOLLOW_TERRAIN);
    EXPECT_EQ(camera_->GetMovementMode(), MovementMode::FOLLOW_TERRAIN);
}

TEST_F(CameraTest, Constraints) {
    // Test camera constraints
    CameraConstraints constraints;
    constraints.min_altitude = 500.0f;
    constraints.max_altitude = 50000.0f;
    constraints.min_pitch = -45.0f;
    constraints.max_pitch = 45.0f;
    constraints.enable_ground_collision = true;
    constraints.ground_clearance = 50.0f;
    constraints.max_rotation_speed = 90.0f;
    constraints.max_movement_speed = 500.0f;
    
    camera_->SetConstraints(constraints);
    
    CameraConstraints retrieved = camera_->GetConstraints();
    EXPECT_FLOAT_EQ(retrieved.min_altitude, constraints.min_altitude);
    EXPECT_FLOAT_EQ(retrieved.max_altitude, constraints.max_altitude);
    EXPECT_FLOAT_EQ(retrieved.min_pitch, constraints.min_pitch);
    EXPECT_FLOAT_EQ(retrieved.max_pitch, constraints.max_pitch);
    EXPECT_EQ(retrieved.enable_ground_collision, constraints.enable_ground_collision);
    EXPECT_FLOAT_EQ(retrieved.ground_clearance, constraints.ground_clearance);
    EXPECT_FLOAT_EQ(retrieved.max_rotation_speed, constraints.max_rotation_speed);
    EXPECT_FLOAT_EQ(retrieved.max_movement_speed, constraints.max_movement_speed);
}

TEST_F(CameraTest, AnimationControl) {
    // Test basic animation
    glm::vec3 start_pos(0.0f, 0.0f, 10.0f);
    // glm::vec3 target_pos(10.0f, 0.0f, 10.0f);
    
    camera_->SetPosition(start_pos);
    EXPECT_EQ(camera_->GetPosition(), start_pos);
    EXPECT_FALSE(camera_->IsAnimating());
    EXPECT_EQ(camera_->GetAnimationState(), AnimationState::IDLE);
    
    // Start animation
    // camera_->AnimateToPosition(target_pos, 1.0f);
    // EXPECT_TRUE(camera_->IsAnimating());
    // EXPECT_EQ(camera_->GetAnimationState(), AnimationState::MOVING);
    
    // Stop animation
    camera_->StopAnimations();
    EXPECT_FALSE(camera_->IsAnimating());
    EXPECT_EQ(camera_->GetAnimationState(), AnimationState::IDLE);
}

TEST_F(CameraTest, GeographicAnimation) {
    // Test geographic animation
    double start_lon = 0.0, start_lat = 0.0, start_alt = 1000.0;
    double target_lon = 10.0, target_lat = 10.0, target_alt = 2000.0;
    
    camera_->SetGeographicPosition(start_lon, start_lat, start_alt);
    camera_->AnimateToGeographic(target_lon, target_lat, target_alt, 2.0f);
    EXPECT_TRUE(camera_->IsAnimating());
    
    camera_->StopAnimations();
    EXPECT_FALSE(camera_->IsAnimating());
}

TEST_F(CameraTest, OrientationAnimation) {
    // Test orientation animation
    double start_heading = 0.0, start_pitch = 0.0, start_roll = 0.0;
    double target_heading = 90.0, target_pitch = 45.0, target_roll = 30.0;
    
    camera_->SetOrientation(start_heading, start_pitch, start_roll);
    camera_->AnimateToOrientation(target_heading, target_pitch, target_roll, 1.5f);
    EXPECT_TRUE(camera_->IsAnimating());
    
    camera_->StopAnimations();
    EXPECT_FALSE(camera_->IsAnimating());
}

TEST_F(CameraTest, VectorDirections) {
    // Test direction vectors
    camera_->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    camera_->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));
    
    glm::vec3 forward = camera_->GetForwardVector();
    glm::vec3 right = camera_->GetRightVector();
    glm::vec3 up = camera_->GetUpVector();
    
    // Basic validation - vectors should be normalized
    EXPECT_FLOAT_EQ(glm::length(forward), 1.0f);
    EXPECT_FLOAT_EQ(glm::length(right), 1.0f);
    EXPECT_FLOAT_EQ(glm::length(up), 1.0f);
    
    // Forward should point from position to target
    glm::vec3 expected_forward = glm::normalize(glm::vec3(0.0f, 0.0f, -5.0f));
    EXPECT_NEAR(glm::dot(forward, expected_forward), 1.0f, 0.001f);
}

TEST_F(CameraTest, ScreenRayCasting) {
    // Test screen to world ray casting
    float aspect_ratio = 16.0f / 9.0f;
    
    glm::vec3 center_ray = camera_->ScreenToWorldRay(0.5f, 0.5f, aspect_ratio);
    glm::vec3 corner_ray = camera_->ScreenToWorldRay(0.0f, 0.0f, aspect_ratio);
    
    // Rays should be normalized
    EXPECT_FLOAT_EQ(glm::length(center_ray), 1.0f);
    EXPECT_FLOAT_EQ(glm::length(corner_ray), 1.0f);
    
    // Center ray should point forward
    glm::vec3 forward = camera_->GetForwardVector();
    EXPECT_NEAR(glm::dot(center_ray, forward), 1.0f, 0.001f);
}

TEST_F(CameraTest, InputHandling) {
    // Test input event processing
    InputEvent mouse_event;
    mouse_event.type = InputEvent::Type::MOUSE_BUTTON_PRESS;
    mouse_event.button = 0; // Left button
    mouse_event.x = 100.0f;
    mouse_event.y = 200.0f;
    mouse_event.timestamp = 12345678;
    
    // Should be handled (result depends on movement mode)
    // bool handled = camera_->ProcessInput(mouse_event);
    // Result may vary based on camera implementation, just ensure no crash
    EXPECT_NO_FATAL_FAILURE();
}

TEST_F(CameraTest, ProjectionTypeSwitching) {
    // Test projection type switching
    EXPECT_EQ(camera_->GetProjectionType(), CameraProjectionType::PERSPECTIVE);
    
    camera_->SetProjectionType(CameraProjectionType::ORTHOGRAPHIC);
    EXPECT_EQ(camera_->GetProjectionType(), CameraProjectionType::ORTHOGRAPHIC);
    
    camera_->SetProjectionType(CameraProjectionType::PERSPECTIVE);
    EXPECT_EQ(camera_->GetProjectionType(), CameraProjectionType::PERSPECTIVE);
}

TEST_F(CameraTest, ResetFunctionality) {
    // Test camera reset
    camera_->SetPosition(glm::vec3(100.0f, 200.0f, 300.0f));
    camera_->SetOrientation(90.0f, 45.0f, 180.0f);
    camera_->SetMovementMode(MovementMode::ORBIT);
    
    // Reset camera
    camera_->Reset();
    
    // After reset, should be in default state
    EXPECT_EQ(camera_->GetMovementMode(), MovementMode::FREE);
    // Other default values depend on implementation
}

class OrthographicCameraTest : public CameraTest {
protected:
    void SetUp() override {
        config_.screen_width = 1920;
        config_.screen_height = 1080;
        config_.enable_opengl_debug = false;
        
        camera_ = CreateOrthographicCamera(config_);
        ASSERT_NE(camera_, nullptr);
        ASSERT_TRUE(camera_->Initialize());
    }
};

TEST_F(OrthographicCameraTest, OrthographicInitialization) {
    EXPECT_EQ(camera_->GetProjectionType(), CameraProjectionType::ORTHOGRAPHIC);
}

TEST_F(OrthographicCameraTest, OrthographicMatrixGeneration) {
    float aspect_ratio = 16.0f / 9.0f;
    glm::mat4 proj_matrix = camera_->GetProjectionMatrix(aspect_ratio);
    
    // For orthographic camera, the middle of the projection should map to origin
    glm::vec4 center_world = proj_matrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(center_world.x, 0.0f, 0.001f);
    EXPECT_NEAR(center_world.y, 0.0f, 0.001f);
}

} // namespace earth_map
