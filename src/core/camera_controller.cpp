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
    
    void SetEcefPosition(const geodesy::EcefPosition& position) override {
        camera_->SetEcefPosition(position);
    }
    
    geodesy::EcefPosition GetEcefPosition() const override {
        return camera_->GetEcefPosition();
    }
    
    void SetGeographicTarget(double longitude, double latitude, double altitude) override {
        camera_->SetGeographicTarget(longitude, latitude, altitude);
    }
    
    void SetEcefTarget(const geodesy::EcefPosition& target) override {
        camera_->SetEcefTarget(target);
    }
    
    geodesy::EcefPosition GetEcefTarget() const override {
        return camera_->GetEcefTarget();
    }

    std::pair<geodesy::EcefPosition, glm::dvec3> ScreenToEcefRay(
        const float screen_x, const float screen_y, const float aspect_ratio) const override {
        return camera_->ScreenToEcefRay(screen_x, screen_y, aspect_ratio);
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

    float GetNearPlane() const override {
        return camera_->GetNearPlane();
    }

    float GetFarPlane() const override {
        return camera_->GetFarPlane();
    }
    
    glm::mat4 GetViewMatrix() const override {
        return camera_->GetViewMatrix();
    }
    
    glm::mat4 GetProjectionMatrix(float aspect_ratio) const override {
        return camera_->GetProjectionMatrix(aspect_ratio);
    }

    glm::vec3 GetForwardVector() const override {
        return camera_->GetForwardVector();
    }

    void SetProjectionType(CameraProjectionType projection_type) override {
        if (camera_->GetProjectionType() == projection_type) {
            return;
        }

        // Projection choice does not change a physical camera state.  Preserve
        // the ECEF pose and metre-based settings while replacing only the
        // projection implementation.
        const geodesy::EcefPosition position = camera_->GetEcefPosition();
        const geodesy::EcefPosition target = camera_->GetEcefTarget();
        const CameraConstraints constraints = camera_->GetConstraints();
        const float field_of_view = camera_->GetFieldOfView();
        const float near_plane = camera_->GetNearPlane();
        const float far_plane = camera_->GetFarPlane();
        const ::earth_map::MovementMode movement_mode = camera_->GetMovementMode();

        switch (projection_type) {
            case CameraProjectionType::PERSPECTIVE:
                camera_ = CreatePerspectiveCamera(config_);
                break;
            case CameraProjectionType::ORTHOGRAPHIC:
                camera_ = CreateOrthographicCamera(config_);
                break;
        }
        camera_->SetConstraints(constraints);
        camera_->SetEcefPosition(position);
        camera_->SetEcefTarget(target);
        camera_->SetFieldOfView(field_of_view);
        camera_->SetClippingPlanes(near_plane, far_plane);
        camera_->SetMovementMode(movement_mode);
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

    bool ProcessInput(const InputEvent& event) override {
        // Forward input event to underlying camera
        return camera_->ProcessInput(event);
    }

    // High-Level API - forward to underlying Camera
    void Zoom(float factor) override {
        camera_->Zoom(factor);
    }

    void Pan(float screen_dx, float screen_dy) override {
        camera_->Pan(screen_dx, screen_dy);
    }

    void Rotate(float delta_heading, float delta_pitch) override {
        camera_->Rotate(delta_heading, delta_pitch);
    }

    void FlyTo(double longitude, double latitude, double altitude_meters,
               float duration_seconds) override {
        camera_->FlyTo(longitude, latitude, altitude_meters, duration_seconds);
    }

    void LookAt(const geodesy::EcefPosition& target) override {
        camera_->LookAt(target);
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
