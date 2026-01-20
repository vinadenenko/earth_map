#define GLM_ENABLE_EXPERIMENTAL
#include "earth_map/core/camera_controller.h"
#include <earth_map/renderer/camera.h>
#include <earth_map/earth_map.h>
#include <earth_map/constants.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <memory>

namespace earth_map {

/**
 * @brief Enhanced camera controller implementation using Phase 5 camera system
 */
class CameraControllerImpl : public CameraController {
public:
    explicit CameraControllerImpl(const Configuration& config) : config_(config) {
        spdlog::info("Creating enhanced camera controller");
        
        // Create perspective camera as default
        camera_ = CreatePerspectiveCamera(config_);
        
        // Set default constraints suitable for globe navigation
        CameraConstraints constraints;
        constraints.min_altitude = constants::camera_constraints::MIN_ALTITUDE_METERS;
        constraints.max_altitude = constants::camera_constraints::MAX_ALTITUDE_METERS;
        constraints.min_pitch = constants::camera_constraints::MIN_PITCH;
        constraints.max_pitch = constants::camera_constraints::MAX_PITCH;
        constraints.enable_ground_collision = true;
        constraints.ground_clearance = constants::camera_constraints::GROUND_CLEARANCE_METERS;
        constraints.max_rotation_speed = constants::camera_constraints::MAX_ROTATION_SPEED;
        constraints.max_movement_speed = constants::camera_constraints::MAX_MOVEMENT_SPEED_METERS;
        
        camera_->SetConstraints(constraints);
        camera_->SetMovementMode(static_cast<::earth_map::MovementMode>(MovementMode::ORBIT));
        
        Reset();
    }
    
    ~CameraControllerImpl() override {
        spdlog::info("Destroying enhanced camera controller");
    }
    
    bool Initialize() override {
        if (initialized_) {
            return true;
        }
        
        spdlog::info("Initializing enhanced camera controller");
        
        if (!camera_->Initialize()) {
            spdlog::error("Failed to initialize camera");
            return false;
        }
        
        initialized_ = true;
        spdlog::info("Enhanced camera controller initialized successfully");
        return true;
    }
    
    void SetGeographicPosition(double longitude, double latitude, double altitude) override {
        camera_->SetGeographicPosition(longitude, latitude, altitude);
    }
    
    void SetPosition(const glm::vec3& position) override {
        camera_->SetPosition(position);
    }
    
    glm::vec3 GetPosition() const override {
        return camera_->GetPosition();
    }
    
    void SetGeographicTarget(double longitude, double latitude, double altitude) override {
        camera_->SetGeographicTarget(longitude, latitude, altitude);
    }
    
    void SetTarget(const glm::vec3& target) override {
        camera_->SetTarget(target);
    }
    
    glm::vec3 GetTarget() const override {
        return camera_->GetTarget();
    }
    
    void SetOrientation(double heading, double pitch, double roll) override {
        camera_->SetOrientation(heading, pitch, roll);
    }
    
    glm::vec3 GetOrientation() const override {
        return camera_->GetOrientation();
    }
    
    void SetFieldOfView(float fov_y) override {
        camera_->SetFieldOfView(fov_y);
    }
    
    float GetFieldOfView() const override {
        return camera_->GetFieldOfView();
    }
    
    void SetClippingPlanes(float near_plane, float far_plane) override {
        camera_->SetClippingPlanes(near_plane, far_plane);
    }
    
    glm::mat4 GetViewMatrix() const override {
        return camera_->GetViewMatrix();
    }
    
    glm::mat4 GetProjectionMatrix(float aspect_ratio) const override {
        return camera_->GetProjectionMatrix(aspect_ratio);
    }
    
    Frustum GetFrustum(float aspect_ratio) const override {
        return camera_->GetFrustum(aspect_ratio);
    }
    
    void SetProjectionType(CameraProjectionType projection_type) override {
        // Recreate camera with new projection type
        switch (projection_type) {
            case CameraProjectionType::PERSPECTIVE:
                camera_ = CreatePerspectiveCamera(config_);
                break;
            case CameraProjectionType::ORTHOGRAPHIC:
                camera_ = CreateOrthographicCamera(config_);
                break;
        }
        
        // Restore current camera state
        if (initialized_) {
            camera_->Initialize();
        }
    }
    
    CameraProjectionType GetProjectionType() const override {
        return static_cast<CameraProjectionType>(camera_->GetProjectionType());
    }
    
    void SetMovementMode(MovementMode movement_mode) override {
        camera_->SetMovementMode(static_cast<::earth_map::MovementMode>(movement_mode));
    }
    
    MovementMode GetMovementMode() const override {
        return static_cast<MovementMode>(camera_->GetMovementMode());
    }
    
    void Update(float delta_time) override {
        camera_->Update(delta_time);
    }

    void Reset() override {
        // Let the base Camera::Reset() handle everything
        camera_->Reset();
    }

private:
    Configuration config_;
    bool initialized_ = false;
    std::unique_ptr<Camera> camera_;
};

// Factory function implementation
CameraController* CreateCameraController(const Configuration& config) {
    return new CameraControllerImpl(config);
}

} // namespace earth_map
