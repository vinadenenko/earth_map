#include <earth_map/renderer/camera.h>
#include <earth_map/earth_map.h>
#include <earth_map/constants.h>
#include <earth_map/coordinates/coordinate_mapper.h>
#include <earth_map/coordinates/coordinate_spaces.h>
#include <earth_map/math/geodetic_calculations.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
// #include <glm/gtx/euler_angles.hpp>
// #include <glm/gtx/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace earth_map {

/**
 * @brief Easing functions for smooth camera animation
 */
namespace Easing {
    float Linear(float t) { return t; }
    
    float EaseInQuad(float t) { return t * t; }
    float EaseOutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
    float EaseInOutQuad(float t) { 
        return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t); 
    }
    
    float EaseInCubic(float t) { return t * t * t; }
    float EaseOutCubic(float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
    float EaseInOutCubic(float t) {
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * (1.0f - t) * (1.0f - t) * (1.0f - t);
    }
    
    float EaseInExpo(float t) { return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f)); }
    float EaseOutExpo(float t) { return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
    float EaseInOutExpo(float t) {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        return t < 0.5f ? 0.5f * std::pow(2.0f, 20.0f * t - 10.0f) : 
                         0.5f * (2.0f - std::pow(2.0f, -20.0f * t + 10.0f));
    }
}

/**
 * @brief Camera animation data
 */
struct CameraAnimation {
    bool active = false;
    float duration = 0.0f;
    float elapsed = 0.0f;
    
    glm::vec3 start_position{0.0f};
    glm::vec3 target_position{0.0f};
    glm::vec3 start_orientation{0.0f};
    glm::vec3 target_orientation{0.0f};
    
    std::function<float(float)> easing_function = Easing::EaseInOutCubic;
    
    void Reset() {
        active = false;
        duration = 0.0f;
        elapsed = 0.0f;
    }
    
    bool IsComplete() const {
        return active && elapsed >= duration;
    }
    
    float GetProgress() const {
        return duration > 0.0f ? std::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;
    }
};

/**
 * @brief Base camera implementation
 */
class CameraImpl : public Camera {
public:
    explicit CameraImpl(const Configuration& config)
        : config_(config) {
        spdlog::info("Creating camera implementation");
        Reset();
    }
    
    virtual ~CameraImpl() {
        spdlog::info("Destroying camera implementation");
    }
    
    bool Initialize() override {
        if (initialized_) {
            return true;
        }
        
        spdlog::info("Initializing camera");
        initialized_ = true;
        
        // Initialize coordinate system
        // if (!coordinate_system_->Initialize()) {
        //     spdlog::error("Failed to initialize coordinate system");
        //     return false;
        // }
        
        spdlog::info("Camera initialized successfully");
        return true;
    }
    
    void Update(float delta_time) override {
        UpdateAnimation(delta_time);
        UpdateMovement(delta_time);
        UpdateViewMatrix();
    }
    
    void SetGeographicPosition(double longitude, double latitude, double altitude) override {
        using namespace earth_map::coordinates;
        Geographic geo(latitude, longitude, altitude);
        World world = CoordinateMapper::GeographicToWorld(geo);
        position_ = world.position;
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
        using namespace earth_map::coordinates;
        Geographic geo(latitude, longitude, altitude);
        World world = CoordinateMapper::GeographicToWorld(geo);
        SetTarget(world.position); // Use SetTarget to also update orientation
    }
    
    void SetTarget(const glm::vec3& target) override {
        target_ = target;

        // Calculate orientation from position→target direction
        glm::vec3 direction = glm::normalize(target - position_);

        // Calculate heading (yaw) from x,z components
        // atan2(x, z) gives angle in XZ plane
        heading_ = glm::degrees(std::atan2(direction.x, direction.z));

        // Calculate pitch from y component
        // Clamp direction.y to [-1, 1] to handle numerical errors
        float clamped_y = std::clamp(direction.y, -1.0f, 1.0f);
        pitch_ = glm::degrees(std::asin(clamped_y));

        // Apply constraints
        pitch_ = std::clamp(pitch_, constraints_.min_pitch, constraints_.max_pitch);

        UpdateViewMatrix();
    }
    
    glm::vec3 GetTarget() const override {
        if (movement_mode_ == MovementMode::ORBIT) {
            // ORBIT mode: Return stored fixed target
            return target_;
        } else {
            // FREE mode: Calculate target from current orientation
            float heading_rad = glm::radians(heading_);
            float pitch_rad = glm::radians(pitch_);

            glm::vec3 forward;
            forward.x = std::cos(pitch_rad) * std::sin(heading_rad);
            forward.y = std::sin(pitch_rad);
            forward.z = std::cos(pitch_rad) * std::cos(heading_rad);
            forward = glm::normalize(forward);

            // Return point at fixed distance from position along forward direction
            return position_ + forward;
        }
    }
    
    void SetOrientation(double heading, double pitch, double roll) override {
        heading_ = static_cast<float>(std::fmod(heading, 360.0));
        pitch_ = static_cast<float>(std::clamp(pitch, static_cast<double>(constraints_.min_pitch), 
                                              static_cast<double>(constraints_.max_pitch)));
        roll_ = static_cast<float>(std::fmod(roll, 360.0));
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
        near_plane_ = std::max(near_plane, 0.001f);
        far_plane_ = std::max(far_plane, near_plane_ + 0.1f);
    }

    float GetNearPlane() const override {
        return near_plane_;
    }

    float GetFarPlane() const override {
        return far_plane_;
    }
    
    glm::mat4 GetViewMatrix() const override {
        return view_matrix_;
    }
    
    glm::mat4 GetProjectionMatrix(float aspect_ratio) const override {
        if (projection_type_ == CameraProjectionType::PERSPECTIVE) {
            return glm::perspective(glm::radians(fov_y_), aspect_ratio, near_plane_, far_plane_);
        } else {
            float half_height = far_plane_ * glm::tan(glm::radians(fov_y_) * 0.5f);
            float half_width = half_height * aspect_ratio;
            return glm::ortho(-half_width, half_width, -half_height, half_height, near_plane_, far_plane_);
        }
    }
    
    glm::mat4 GetViewProjectionMatrix(float aspect_ratio) const override {
        return GetProjectionMatrix(aspect_ratio) * GetViewMatrix();
    }
    
    Frustum GetFrustum(float aspect_ratio) const override {
        glm::mat4 view_projection = GetViewProjectionMatrix(aspect_ratio);
        return Frustum(view_projection);
    }
    
    void SetProjectionType(CameraProjectionType projection_type) override {
        projection_type_ = projection_type;
    }
    
    CameraProjectionType GetProjectionType() const override {
        return projection_type_;
    }
    
    void SetMovementMode(MovementMode movement_mode) override {
        movement_mode_ = movement_mode;
    }
    
    MovementMode GetMovementMode() const override {
        return movement_mode_;
    }
    
    void SetConstraints(const CameraConstraints& constraints) override {
        constraints_ = constraints;
    }
    
    CameraConstraints GetConstraints() const override {
        return constraints_;
    }
    
    bool ProcessInput(const InputEvent& event) override {
        switch (event.type) {
            case InputEvent::Type::MOUSE_MOVE:
                return HandleMouseMove(event);
            case InputEvent::Type::MOUSE_BUTTON_PRESS:
                return HandleMousePress(event);
            case InputEvent::Type::MOUSE_BUTTON_RELEASE:
                return HandleMouseRelease(event);
            case InputEvent::Type::MOUSE_SCROLL:
                return HandleMouseScroll(event);
            case InputEvent::Type::KEY_PRESS:
                return HandleKeyPress(event);
            case InputEvent::Type::KEY_RELEASE:
                return HandleKeyRelease(event);
            case InputEvent::Type::DOUBLE_CLICK:
                return HandleDoubleClick(event);
            default:
                return false;
        }
    }
    
    AnimationState GetAnimationState() const override {
        if (!animation_.active) {
            return AnimationState::IDLE;
        }
        
        if (animation_.IsComplete()) {
            return AnimationState::IDLE;
        }
        
        // Determine animation type based on what's changing
        if (glm::length(animation_.target_position - animation_.start_position) > 0.01f) {
            return AnimationState::MOVING;
        }
        if (glm::length(animation_.target_orientation - animation_.start_orientation) > 0.01f) {
            return AnimationState::ROTATING;
        }
        return AnimationState::IDLE;
    }
    
    bool IsAnimating() const override {
        return animation_.active && !animation_.IsComplete();
    }
    
    void Reset() override {
        // Set default position looking at the globe (normalized units)
        position_ = glm::vec3(0.0f, 0.0f, constants::camera::DEFAULT_CAMERA_DISTANCE_NORMALIZED);
        up_ = glm::vec3(0.0f, 1.0f, 0.0f);

        roll_ = 0.0f;

        fov_y_ = constants::camera::DEFAULT_FOV;
        near_plane_ = constants::camera::DEFAULT_NEAR_PLANE_NORMALIZED;
        far_plane_ = constants::camera::DEFAULT_FAR_PLANE_NORMALIZED;

        projection_type_ = CameraProjectionType::PERSPECTIVE;
        movement_mode_ = MovementMode::ORBIT;  // Default to ORBIT mode

        // Reset movement state
        movement_forward_ = movement_right_ = movement_up_ = 0.0f;
        rotation_x_ = rotation_y_ = rotation_z_ = 0.0f;

        // Set target and calculate orientation from position→target direction
        SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));

        animation_.Reset();
    }
    
    void AnimateToGeographic(double longitude, double latitude, double altitude, float duration) override {
        using namespace earth_map::coordinates;
        Geographic geo(latitude, longitude, altitude);
        World world = CoordinateMapper::GeographicToWorld(geo);

        animation_.start_position = position_;
        animation_.target_position = world.position;
        animation_.start_orientation = glm::vec3(heading_, pitch_, roll_);
        animation_.duration = duration;
        animation_.elapsed = 0.0f;
        animation_.active = true;
        animation_.easing_function = Easing::EaseInOutCubic;
    }
    
    void AnimateToOrientation(double heading, double pitch, double roll, float duration) override {
        animation_.start_orientation = glm::vec3(heading_, pitch_, roll_);
        animation_.target_orientation = glm::vec3(
            static_cast<float>(heading), 
            static_cast<float>(pitch), 
            static_cast<float>(roll)
        );
        animation_.duration = duration;
        animation_.elapsed = 0.0f;
        animation_.active = true;
        animation_.easing_function = Easing::EaseInOutCubic;
    }
    
    void StopAnimations() override {
        animation_.Reset();
    }
    
    glm::vec3 GetForwardVector() const override {
        // Calculate forward from orientation (consistent with UpdateViewMatrix)
        float heading_rad = glm::radians(heading_);
        float pitch_rad = glm::radians(pitch_);

        glm::vec3 forward;
        forward.x = std::cos(pitch_rad) * std::sin(heading_rad);
        forward.y = std::sin(pitch_rad);
        forward.z = std::cos(pitch_rad) * std::cos(heading_rad);

        return glm::normalize(forward);
    }
    
    glm::vec3 GetRightVector() const override {
        glm::vec3 forward = GetForwardVector();
        return glm::normalize(glm::cross(forward, up_));
    }
    
    glm::vec3 GetUpVector() const override {
        return up_;
    }
    
    glm::vec3 ScreenToWorldRay(float screen_x, float screen_y, float aspect_ratio) const override {
        // Convert screen coordinates to normalized device coordinates
        glm::vec4 ndc(
            2.0f * screen_x - 1.0f,
            1.0f - 2.0f * screen_y, // Flip Y
            -1.0f, // Near plane
            1.0f
        );
        
        // Convert to eye coordinates
        glm::mat4 projection = GetProjectionMatrix(aspect_ratio);
        glm::mat4 view = GetViewMatrix();
        glm::mat4 inv_projection = glm::inverse(projection);
        glm::mat4 inv_view = glm::inverse(view);
        
        glm::vec4 eye_ray = inv_projection * ndc;
        eye_ray = glm::vec4(eye_ray.x, eye_ray.y, -1.0f, 0.0f);
        
        // Convert to world coordinates
        glm::vec4 world_ray = inv_view * eye_ray;
        
        return glm::normalize(glm::vec3(world_ray));
    }

protected:
    Configuration config_;
    bool initialized_ = false;

    // Camera position and orientation
    glm::vec3 position_{0.0f};
    glm::vec3 target_{0.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    
    // Orientation angles (degrees)
    float heading_ = 0.0f;
    float pitch_ = 0.0f;
    float roll_ = 0.0f;
    
    // Projection parameters
    float fov_y_ = constants::camera::DEFAULT_FOV;
    float near_plane_ = constants::camera::DEFAULT_NEAR_PLANE_NORMALIZED;
    float far_plane_ = constants::camera::DEFAULT_FAR_PLANE_NORMALIZED;
    
    // Camera settings
    CameraProjectionType projection_type_ = CameraProjectionType::PERSPECTIVE;
    MovementMode movement_mode_ = MovementMode::ORBIT;
    CameraConstraints constraints_;
    
    // Movement state
    float movement_forward_ = 0.0f;
    float movement_right_ = 0.0f;
    float movement_up_ = 0.0f;
    float rotation_x_ = 0.0f;
    float rotation_y_ = 0.0f;
    float rotation_z_ = 0.0f;
    
    // Animation state
    CameraAnimation animation_;
    
    // Mouse interaction state
    bool mouse_dragging_ = false;
    bool middle_mouse_dragging_ = false;
    int active_mouse_button_ = -1;  // Track which button is active
    glm::vec2 last_mouse_pos_{0.0f};
    uint64_t last_mouse_time_ = 0;
    
    // Matrices
    glm::mat4 view_matrix_ = glm::mat4(1.0f);
    
    void UpdateViewMatrix() {
        glm::vec3 computed_target;

        if (movement_mode_ == MovementMode::ORBIT) {
            // ORBIT mode: Use stored fixed target
            computed_target = target_;
        } else {
            // FREE mode: Compute target from orientation
            float heading_rad = glm::radians(heading_);
            float pitch_rad = glm::radians(pitch_);

            glm::vec3 forward;
            forward.x = std::cos(pitch_rad) * std::sin(heading_rad);
            forward.y = std::sin(pitch_rad);
            forward.z = std::cos(pitch_rad) * std::cos(heading_rad);

            computed_target = position_ + glm::normalize(forward);
        }

        view_matrix_ = glm::lookAt(position_, computed_target, up_);
    }
    
    void UpdateAnimation(float delta_time) {
        if (!animation_.active) {
            return;
        }
        
        animation_.elapsed += delta_time;
        
        if (animation_.IsComplete()) {
            // Apply final values
            position_ = animation_.target_position;
            heading_ = animation_.target_orientation.x;
            pitch_ = animation_.target_orientation.y;
            roll_ = animation_.target_orientation.z;
            animation_.Reset();
            return;
        }
        
        // Apply interpolated values
        float progress = animation_.GetProgress();
        float eased_progress = animation_.easing_function(progress);
        
        position_ = glm::mix(animation_.start_position, animation_.target_position, eased_progress);
        
        glm::vec3 current_orientation = glm::mix(animation_.start_orientation, 
                                               animation_.target_orientation, 
                                               eased_progress);
        heading_ = current_orientation.x;
        pitch_ = current_orientation.y;
        roll_ = current_orientation.z;
    }
    
    void UpdateMovement(float delta_time) {
        if (movement_mode_ != MovementMode::FREE) {
            return;
        }

        glm::vec3 forward = GetForwardVector();
        glm::vec3 right = GetRightVector();
        glm::vec3 up = GetUpVector();

        // Convert speed from meters/second to normalized units/second
        // Camera is in normalized space (radius = 1.0), but constraints are in meters
        float speed_meters = constraints_.max_movement_speed;  // meters/second
        float speed_normalized = constants::conversion::MetersToNormalized(speed_meters);

        // Scale movement speed based on altitude (faster when higher, slower when closer to surface)
        float distance_from_origin = glm::length(position_);
        float altitude_factor = std::max(0.1f, distance_from_origin - 1.0f);  // Relative to normalized radius
        float adaptive_speed = speed_normalized * (1.0f + altitude_factor * 2.0f);

        float rot_speed = constraints_.max_rotation_speed;

        // Update position
        if (glm::abs(movement_forward_) > 0.01f) {
            position_ += forward * movement_forward_ * adaptive_speed * delta_time;
        }
        if (glm::abs(movement_right_) > 0.01f) {
            position_ += right * movement_right_ * adaptive_speed * delta_time;
        }
        if (glm::abs(movement_up_) > 0.01f) {
            position_ += up * movement_up_ * adaptive_speed * delta_time;
        }
        
        // Update orientation
        if (glm::abs(rotation_x_) > 0.01f) {
            heading_ += rotation_x_ * rot_speed * delta_time;
            heading_ = std::fmod(heading_, 360.0f);
        }
        if (glm::abs(rotation_y_) > 0.01f) {
            pitch_ += rotation_y_ * rot_speed * delta_time;
            pitch_ = std::clamp(pitch_, constraints_.min_pitch, constraints_.max_pitch);
        }
        if (glm::abs(rotation_z_) > 0.01f) {
            roll_ += rotation_z_ * rot_speed * delta_time;
            roll_ = std::fmod(roll_, 360.0f);
        }

        // Apply decay to movement impulses (from scroll wheel)
        // This prevents continuous movement after scroll
        float decay_rate = 10.0f;  // Higher = faster decay
        movement_forward_ *= std::exp(-decay_rate * delta_time);
        movement_right_ *= std::exp(-decay_rate * delta_time);
        movement_up_ *= std::exp(-decay_rate * delta_time);

        // Zero out very small values
        if (std::abs(movement_forward_) < 0.001f) movement_forward_ = 0.0f;
        if (std::abs(movement_right_) < 0.001f) movement_right_ = 0.0f;
        if (std::abs(movement_up_) < 0.001f) movement_up_ = 0.0f;

        // Update target based on orientation
        glm::quat q = glm::quat(glm::radians(glm::vec3(pitch_, heading_, roll_)));
        glm::vec3 direction = q * glm::vec3(0.0f, 0.0f, -1.0f);
        target_ = position_ + direction * 1000.0f; // 1km look distance
    }
    
    bool HandleMouseMove(const InputEvent& event) {
        if (!mouse_dragging_ && !middle_mouse_dragging_) {
            return false;
        }

        glm::vec2 current_pos(event.x, event.y);
        glm::vec2 delta = current_pos - last_mouse_pos_;

        // Middle mouse button: Tilt and rotate camera (pitch and heading)
        if (middle_mouse_dragging_ && active_mouse_button_ == 2) {
            if (movement_mode_ == MovementMode::ORBIT) {
                // Tilt mode: change pitch (up/down drag) and heading (left/right drag)
                float sensitivity = 0.3f;

                // Horizontal drag: rotate heading around the target
                heading_ -= delta.x * sensitivity;
                heading_ = std::fmod(heading_, 360.0f);

                // Vertical drag: tilt pitch
                pitch_ -= delta.y * sensitivity;
                pitch_ = std::clamp(pitch_, constraints_.min_pitch, constraints_.max_pitch);

                // Recalculate camera position based on new heading and pitch
                glm::vec3 offset = position_ - target_;
                float distance = glm::length(offset);

                // Convert heading and pitch to spherical coordinates
                float heading_rad = glm::radians(heading_);
                float pitch_rad = glm::radians(pitch_);

                // Calculate new camera position
                offset.x = distance * std::cos(pitch_rad) * std::sin(heading_rad);
                offset.y = distance * std::sin(pitch_rad);
                offset.z = distance * std::cos(pitch_rad) * std::cos(heading_rad);

                position_ = target_ + offset;
            } else {
                // Free mode: adjust pitch and heading
                float sensitivity = 0.2f;
                heading_ -= delta.x * sensitivity;
                heading_ = std::fmod(heading_, 360.0f);

                pitch_ -= delta.y * sensitivity;
                pitch_ = std::clamp(pitch_, constraints_.min_pitch, constraints_.max_pitch);
            }
        }
        // Left mouse button: Standard orbit/rotation
        else if (mouse_dragging_ && active_mouse_button_ == 0) {
            if (movement_mode_ == MovementMode::ORBIT) {
                // Orbital camera controls
                float sensitivity = 0.1f;

                // Rotate around target
                glm::vec3 offset = position_ - target_;

                // Horizontal rotation (around Y axis)
                glm::quat horiz_rot = glm::angleAxis(-delta.x * sensitivity * 0.01f, glm::vec3(0.0f, 1.0f, 0.0f));
                offset = horiz_rot * offset;

                // Vertical rotation (around right vector)
                glm::vec3 right = glm::normalize(glm::cross(glm::normalize(offset), glm::vec3(0.0f, 1.0f, 0.0f)));
                glm::quat vert_rot = glm::angleAxis(delta.y * sensitivity * 0.01f, right);
                offset = vert_rot * offset;

                position_ = target_ + offset;
            } else {
                // Free camera controls
                float sensitivity = 0.2f;
                rotation_x_ = -delta.x * sensitivity;
                rotation_y_ = -delta.y * sensitivity;
            }
        }

        last_mouse_pos_ = current_pos;
        last_mouse_time_ = event.timestamp;
        return true;
    }
    
    bool HandleMousePress(const InputEvent& event) {
        if (event.button == 0) { // Left mouse button
            mouse_dragging_ = true;
            active_mouse_button_ = 0;
            last_mouse_pos_ = glm::vec2(event.x, event.y);
            last_mouse_time_ = event.timestamp;
            return true;
        } else if (event.button == 2) { // Middle mouse button (button 2 in GLFW)
            middle_mouse_dragging_ = true;
            active_mouse_button_ = 2;
            last_mouse_pos_ = glm::vec2(event.x, event.y);
            last_mouse_time_ = event.timestamp;
            return true;
        }
        return false;
    }
    
    bool HandleMouseRelease(const InputEvent& event) {
        if (event.button == 0) { // Left mouse button
            mouse_dragging_ = false;
            if (active_mouse_button_ == 0) {
                active_mouse_button_ = -1;
            }
            rotation_x_ = 0.0f;
            rotation_y_ = 0.0f;
            return true;
        } else if (event.button == 2) { // Middle mouse button
            middle_mouse_dragging_ = false;
            if (active_mouse_button_ == 2) {
                active_mouse_button_ = -1;
            }
            return true;
        }
        return false;
    }
    
    bool HandleMouseScroll(const InputEvent& event) {
        // Zoom factor: how much to zoom per scroll tick
        // Positive scroll = zoom in (decrease distance), negative = zoom out (increase distance)
        float zoom_factor = 0.5f;

        if (movement_mode_ == MovementMode::ORBIT) {
            // Zoom in orbital mode
            glm::vec3 offset = position_ - target_;
            float current_distance = glm::length(offset);

            // Calculate zoom speed based on current distance (zoom faster when far, slower when close)
            // float zoom_speed = 1.0f - (event.scroll_delta * zoom_factor);
            // float new_distance = current_distance * zoom_speed;

            // Apply constraints (in normalized units)
            float min_distance = constants::camera_constraints::MIN_DISTANCE_NORMALIZED;
            float max_distance = constants::camera_constraints::MAX_DISTANCE_NORMALIZED;
            // new_distance = std::clamp(new_distance, min_distance, max_distance);

            float normalized =
                (current_distance - min_distance) /
                (max_distance - min_distance);

            // Keep in [0, 1]
            normalized = std::clamp(normalized, 0.0f, 1.0f);

            // Ease zoom-in near the surface (squared) to avoid overshooting.
            // Zoom-out uses linear factor so the camera can always pull away.
            const float slow_factor = (event.scroll_delta > 0.0f)
                ? normalized * normalized
                : normalized;

            // Convert scroll into distance delta
            float zoom_delta = event.scroll_delta * zoom_factor * slow_factor;

            // Apply additively (not multiplicatively)
            float new_distance = current_distance - zoom_delta;
            new_distance = std::clamp(new_distance, min_distance, max_distance);

            // Only update if distance actually changed (not clamped)
            if (std::abs(new_distance - current_distance) > 0.0001f) {
                offset = glm::normalize(offset) * new_distance;
                position_ = target_ + offset;
            }
        } else {
            // Move forward/backward in free mode using scroll
            // Convert scroll to movement impulse
            movement_forward_ = event.scroll_delta * 2.0f;
        }

        return true;
    }
    
    bool HandleKeyPress(const InputEvent& event) {
        switch (event.key) {
            case 'W':
            case 265: // Up arrow
                movement_forward_ = 1.0f;
                return true;
            case 'S':
            case 264: // Down arrow
                movement_forward_ = -1.0f;
                return true;
            case 'A':
            case 263: // Left arrow
                movement_right_ = -1.0f;
                return true;
            case 'D':
            case 262: // Right arrow
                movement_right_ = 1.0f;
                return true;
            case 'Q':
                movement_up_ = 1.0f;
                return true;
            case 'E':
                movement_up_ = -1.0f;
                return true;
            default:
                return false;
        }
    }
    
    bool HandleKeyRelease(const InputEvent& event) {
        switch (event.key) {
            case 'W':
            case 'S':
            case 265: // Up arrow
            case 264: // Down arrow
                movement_forward_ = 0.0f;
                return true;
            case 'A':
            case 'D':
            case 263: // Left arrow
            case 262: // Right arrow
                movement_right_ = 0.0f;
                return true;
            case 'Q':
            case 'E':
                movement_up_ = 0.0f;
                return true;
            default:
                return false;
        }
    }
    
    bool HandleDoubleClick(const InputEvent& event) {
        // Perform ray casting to find click location on globe
        // Convert mouse coordinates to ray in world space

        // Use screen dimensions from config (this is approximate - ideally we'd get actual viewport)
        int screen_width = config_.screen_width;
        int screen_height = config_.screen_height;
        glm::ivec4 viewport(0, 0, screen_width, screen_height);

        // Get camera matrices
        float aspect_ratio = static_cast<float>(screen_width) / screen_height;
        glm::mat4 projection = GetProjectionMatrix(aspect_ratio);
        glm::mat4 view = GetViewMatrix();

        // Convert screen coordinates to NDC (-1 to 1)
        // Note: GLFW Y=0 at top, OpenGL Y=0 at bottom
        float ndc_x = (event.x / screen_width) * 2.0f - 1.0f;
        float ndc_y = 1.0f - (event.y / screen_height) * 2.0f;

        // Unproject to world space
        glm::vec4 ray_clip(ndc_x, ndc_y, -1.0f, 1.0f);
        glm::vec4 ray_eye = glm::inverse(projection) * ray_clip;
        ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0f, 0.0f);
        glm::vec3 ray_world = glm::vec3(glm::inverse(view) * ray_eye);
        ray_world = glm::normalize(ray_world);

        // Ray-sphere intersection with globe (sphere at origin with radius 1.0)
        glm::vec3 ray_origin = position_;
        glm::vec3 ray_dir = ray_world;
        glm::vec3 sphere_center(0.0f, 0.0f, 0.0f);
        float sphere_radius = 1.0f;  // Normalized globe radius

        // Ray-sphere intersection
        glm::vec3 oc = ray_origin - sphere_center;
        float a = glm::dot(ray_dir, ray_dir);
        float b = 2.0f * glm::dot(oc, ray_dir);
        float c = glm::dot(oc, oc) - sphere_radius * sphere_radius;
        float discriminant = b * b - 4.0f * a * c;

        if (discriminant >= 0.0f) {
            // Ray hits the globe
            float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
            glm::vec3 hit_point = ray_origin + ray_dir * t;

            // Animate camera to look at this point
            if (movement_mode_ == MovementMode::ORBIT) {
                // Set new target to clicked point
                glm::vec3 new_target = hit_point;

                // Calculate new camera position: maintain current distance but point at new target
                glm::vec3 current_offset = position_ - target_;
                float current_distance = glm::length(current_offset);

                // Zoom in to a closer distance (50% of current distance, but not too close)
                float zoom_factor = 0.4f;
                float new_distance = std::max(current_distance * zoom_factor,
                                             constants::camera_constraints::MIN_DISTANCE_NORMALIZED * 2.0f);

                // Calculate new position: from target toward camera direction
                glm::vec3 to_camera = glm::normalize(position_ - new_target);
                glm::vec3 new_position = new_target + to_camera * new_distance;

                // Animate to new position and target
                animation_.start_position = position_;
                animation_.target_position = new_position;
                animation_.start_orientation = glm::vec3(heading_, pitch_, roll_);
                animation_.target_orientation = animation_.start_orientation;  // Keep orientation
                animation_.duration = 0.8f;  // Smooth animation
                animation_.elapsed = 0.0f;
                animation_.active = true;
                animation_.easing_function = Easing::EaseInOutCubic;

                // Update target immediately so camera knows where to look during animation
                target_ = new_target;

                spdlog::info("Double-click: zooming to position ({:.3f}, {:.3f}, {:.3f})",
                           new_target.x, new_target.y, new_target.z);
            }
        } else {
            // Ray missed the globe - just zoom in toward current target
            if (movement_mode_ == MovementMode::ORBIT) {
                glm::vec3 offset = position_ - target_;
                float new_distance = glm::length(offset) * 0.5f;

                // Apply min distance constraint
                new_distance = std::max(new_distance, constants::camera_constraints::MIN_DISTANCE_NORMALIZED);

                offset = glm::normalize(offset) * new_distance;
                glm::vec3 new_position = target_ + offset;

                AnimateToPosition(new_position, 0.5f);
            }
        }

        return true;
    }
    
    void AnimateToPosition(const glm::vec3& target_pos, float duration) {
        animation_.start_position = position_;
        animation_.target_position = target_pos;
        animation_.start_orientation = glm::vec3(heading_, pitch_, roll_);
        animation_.duration = duration;
        animation_.elapsed = 0.0f;
        animation_.active = true;
        animation_.easing_function = Easing::EaseInOutCubic;
    }
};

/**
 * @brief Perspective camera implementation
 */
class PerspectiveCamera : public CameraImpl {
public:
    explicit PerspectiveCamera(const Configuration& config) : CameraImpl(config) {
        SetProjectionType(CameraProjectionType::PERSPECTIVE);
    }
};

/**
 * @brief Orthographic camera implementation
 */
class OrthographicCamera : public CameraImpl {
public:
    explicit OrthographicCamera(const Configuration& config) : CameraImpl(config) {
        SetProjectionType(CameraProjectionType::ORTHOGRAPHIC);
    }
};

// Factory functions
std::unique_ptr<Camera> CreatePerspectiveCamera(const Configuration& config) {
    return std::make_unique<PerspectiveCamera>(config);
}

std::unique_ptr<Camera> CreateOrthographicCamera(const Configuration& config) {
    return std::make_unique<OrthographicCamera>(config);
}

} // namespace earth_map
