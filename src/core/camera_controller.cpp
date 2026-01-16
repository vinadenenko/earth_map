#include <earth_map/core/camera_controller.h>
#include <earth_map/earth_map.h>
#include <earth_map/math/frustum.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <cmath>

namespace earth_map {

/**
 * @brief Basic camera controller implementation
 */
class CameraControllerImpl : public CameraController {
public:
    explicit CameraControllerImpl(const Configuration& config) : config_(config) {
        spdlog::info("Creating camera controller");
        Reset();
    }
    
    ~CameraControllerImpl() override {
        spdlog::info("Destroying camera controller");
    }
    
    bool Initialize() override {
        if (initialized_) {
            return true;
        }
        
        spdlog::info("Initializing camera controller");
        initialized_ = true;
        
        spdlog::info("Camera controller initialized successfully");
        return true;
    }
    
    void SetGeographicPosition(double longitude, double latitude, double altitude) override {
        // Convert geographic coordinates to Cartesian coordinates
        // This is a simplified conversion for demo purposes
        const double earth_radius = 6371000.0; // Earth radius in meters
        
        double lat_rad = glm::radians(latitude);
        double lon_rad = glm::radians(longitude);
        double r = earth_radius + altitude;
        
        position_.x = static_cast<float>(r * cos(lat_rad) * cos(lon_rad));
        position_.y = static_cast<float>(r * sin(lat_rad));
        position_.z = static_cast<float>(r * cos(lat_rad) * sin(lon_rad));
        
        UpdateViewMatrix();
    }
    
    void SetPosition(const glm::vec3& position) override {
        position_ = position;
        UpdateViewMatrix();
    }
    
    glm::vec3 GetPosition() const override {
        return position_;
    }
    
    void SetGeographicTarget(double longitude, double latitude, double altitude) override {
        // Convert geographic coordinates to Cartesian coordinates
        const double earth_radius = 6371000.0;
        
        double lat_rad = glm::radians(latitude);
        double lon_rad = glm::radians(longitude);
        double r = earth_radius + altitude;
        
        target_.x = static_cast<float>(r * cos(lat_rad) * cos(lon_rad));
        target_.y = static_cast<float>(r * sin(lat_rad));
        target_.z = static_cast<float>(r * cos(lat_rad) * sin(lon_rad));
        
        UpdateViewMatrix();
    }
    
    void SetTarget(const glm::vec3& target) override {
        target_ = target;
        UpdateViewMatrix();
    }
    
    glm::vec3 GetTarget() const override {
        return target_;
    }
    
    void SetOrientation(double heading, double pitch, double roll) override {
        heading_ = static_cast<float>(heading);
        pitch_ = static_cast<float>(pitch);
        roll_ = static_cast<float>(roll);
        UpdateViewMatrix();
    }
    
    glm::vec3 GetOrientation() const override {
        return glm::vec3(heading_, pitch_, roll_);
    }
    
    void SetFieldOfView(float fov_y) override {
        fov_y_ = glm::clamp(fov_y, 1.0f, 179.0f);
    }
    
    float GetFieldOfView() const override {
        return fov_y_;
    }
    
    void SetClippingPlanes(float near_plane, float far_plane) override {
        near_plane_ = near_plane;
        far_plane_ = far_plane;
    }
    
    glm::mat4 GetViewMatrix() const override {
        return view_matrix_;
    }
    
    glm::mat4 GetProjectionMatrix(float aspect_ratio) const override {
        if (projection_type_ == ProjectionType::PERSPECTIVE) {
            return glm::perspective(glm::radians(fov_y_), aspect_ratio, near_plane_, far_plane_);
        } else {
            float half_height = far_plane_ * glm::tan(glm::radians(fov_y_) * 0.5f);
            float half_width = half_height * aspect_ratio;
            return glm::ortho(-half_width, half_width, -half_height, half_height, near_plane_, far_plane_);
        }
    }
    
    Frustum GetFrustum(float aspect_ratio) const override {
        (void)aspect_ratio; // Suppress unused parameter warning
        // Create a simple frustum (placeholder implementation)
        Frustum frustum;
        // TODO: Implement actual frustum calculation
        return frustum;
    }
    
    void SetProjectionType(ProjectionType projection_type) override {
        projection_type_ = projection_type;
    }
    
    ProjectionType GetProjectionType() const override {
        return projection_type_;
    }
    
    void SetMovementMode(MovementMode movement_mode) override {
        movement_mode_ = movement_mode;
    }
    
    MovementMode GetMovementMode() const override {
        return movement_mode_;
    }
    
    void Update(float delta_time) override {
        // Update camera animations and movement
        if (movement_mode_ == MovementMode::ORBIT) {
            // Simple orbit animation for demo
            float rotation_speed = 0.2f; // radians per second
            float angle = rotation_speed * delta_time;
            
            glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
            position_ = glm::vec3(rotation * glm::vec4(position_, 1.0f));
            
            UpdateViewMatrix();
        }
    }
    
    void Reset() override {
        // Set default position looking at the globe
        position_ = glm::vec3(0.0f, 0.0f, 3.0f);
        target_ = glm::vec3(0.0f, 0.0f, 0.0f);
        up_ = glm::vec3(0.0f, 1.0f, 0.0f);
        
        heading_ = 0.0f;
        pitch_ = 0.0f;
        roll_ = 0.0f;
        
        fov_y_ = 45.0f;
        near_plane_ = 0.1f;
        far_plane_ = 1000.0f;
        
        projection_type_ = ProjectionType::PERSPECTIVE;
        movement_mode_ = MovementMode::FREE;
        
        UpdateViewMatrix();
    }

private:
    Configuration config_;
    bool initialized_ = false;
    
    // Camera position and orientation
    glm::vec3 position_ = glm::vec3(0.0f, 0.0f, 3.0f);
    glm::vec3 target_ = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up_ = glm::vec3(0.0f, 1.0f, 0.0f);
    
    // Orientation angles (degrees)
    float heading_ = 0.0f;
    float pitch_ = 0.0f;
    float roll_ = 0.0f;
    
    // Projection parameters
    float fov_y_ = 45.0f;
    float near_plane_ = 0.1f;
    float far_plane_ = 1000.0f;
    
    // Camera settings
    ProjectionType projection_type_ = ProjectionType::PERSPECTIVE;
    MovementMode movement_mode_ = MovementMode::FREE;
    
    // Matrices
    glm::mat4 view_matrix_ = glm::mat4(1.0f);
    
    void UpdateViewMatrix() {
        view_matrix_ = glm::lookAt(position_, target_, up_);
    }
};

// Factory function - for now, create in the constructor
// In the future, this might be moved to a factory pattern
CameraController* CreateCameraController(const Configuration& config) {
    return new CameraControllerImpl(config);
}

} // namespace earth_map
