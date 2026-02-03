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
#include <set>

namespace earth_map {

/**
 * @brief Internal camera state for validation
 *
 * All state mutations flow through ApplyState() which validates
 * and clamps this struct to enforce camera constraints.
 */
struct CameraState {
    glm::vec3 position{0.0f};
    float heading = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

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
        UpdateClippingPlanes();
        UpdateViewMatrix();
    }
    
    void SetGeographicPosition(double longitude, double latitude, double altitude) override {
        using namespace earth_map::coordinates;
        Geographic geo(latitude, longitude, altitude);
        World world = CoordinateMapper::GeographicToWorld(geo);

        CameraState state = GetCurrentState();
        state.position = world.position;
        ApplyState(state);
        SetFromState(state);
        UpdateViewMatrix();
    }

    void SetPosition(const glm::vec3& position) override {
        CameraState state = GetCurrentState();
        state.position = position;
        ApplyState(state);
        SetFromState(state);
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
        float new_heading = glm::degrees(std::atan2(direction.x, direction.z));

        // Calculate pitch from y component
        // Clamp direction.y to [-1, 1] to handle numerical errors
        float clamped_y = std::clamp(direction.y, -1.0f, 1.0f);
        float new_pitch = glm::degrees(std::asin(clamped_y));

        // Apply constraints via ApplyState
        CameraState state = GetCurrentState();
        state.heading = new_heading;
        state.pitch = new_pitch;
        ApplyState(state);
        SetFromState(state);

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
        CameraState state = GetCurrentState();
        state.heading = static_cast<float>(heading);
        state.pitch = static_cast<float>(pitch);
        state.roll = static_cast<float>(roll);
        ApplyState(state);
        SetFromState(state);
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

    // =========================================================================
    // High-Level Camera Control API Implementation
    // =========================================================================

    void Zoom(float factor) override {
        if (factor <= 0.0f) {
            return;  // Invalid factor
        }

        CameraState state = GetCurrentState();

        // Both ORBIT and FREE modes: zoom relative to ORIGIN (globe center).
        // This ensures ApplyState() distance constraints work correctly,
        // since ApplyState always checks distance from origin.
        // In ORBIT mode, target_ should always be origin anyway.
        float current_distance = glm::length(state.position);

        if (current_distance > 0.0001f) {
            float new_distance = current_distance * factor;
            state.position = glm::normalize(state.position) * new_distance;
        }

        // ApplyState enforces distance constraints relative to origin
        ApplyState(state);
        SetFromState(state);
        UpdateViewMatrix();
    }

    void Pan(float screen_dx, float screen_dy) override {
        CameraState state = GetCurrentState();

        // Sensitivity scales with altitude for consistent feel
        float distance = glm::length(state.position);
        float sensitivity = distance * 0.001f;

        if (movement_mode_ == MovementMode::ORBIT) {
            // ORBIT mode: rotate view around the globe
            // Horizontal movement changes heading (longitude-like)
            // Vertical movement changes pitch (latitude-like)
            state.heading -= screen_dx * sensitivity * 10.0f;
            state.pitch -= screen_dy * sensitivity * 10.0f;
        } else {
            // FREE mode: translate camera position
            glm::vec3 right = GetRightVector();
            glm::vec3 up = GetUpVector();

            state.position += right * screen_dx * sensitivity;
            state.position -= up * screen_dy * sensitivity;  // Screen Y is inverted
        }

        ApplyState(state);
        SetFromState(state);
        UpdateViewMatrix();
    }

    void Rotate(float delta_heading, float delta_pitch) override {
        CameraState state = GetCurrentState();

        state.heading += delta_heading;
        state.pitch += delta_pitch;

        // ApplyState handles pitch clamping and heading normalization
        ApplyState(state);
        SetFromState(state);
        UpdateViewMatrix();
    }

    void FlyTo(double longitude, double latitude, double altitude_meters,
               float duration_seconds) override {
        using namespace earth_map::coordinates;

        // Clamp altitude to constraints before starting animation
        double clamped_altitude = std::clamp(
            altitude_meters,
            static_cast<double>(constraints_.min_altitude),
            static_cast<double>(constraints_.max_altitude)
        );

        Geographic geo(latitude, longitude, clamped_altitude);
        World world = CoordinateMapper::GeographicToWorld(geo);

        // Validate target position with ApplyState
        CameraState target_state;
        target_state.position = world.position;
        target_state.heading = heading_;  // Keep current orientation initially
        target_state.pitch = pitch_;
        target_state.roll = roll_;
        ApplyState(target_state);

        // Calculate orientation to look at globe center from new position
        glm::vec3 direction = glm::normalize(glm::vec3(0.0f) - target_state.position);
        float target_heading = glm::degrees(std::atan2(direction.x, direction.z));
        float target_pitch = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
        target_pitch = std::clamp(target_pitch, constraints_.min_pitch, constraints_.max_pitch);

        // Set up animation
        animation_.start_position = position_;
        animation_.target_position = target_state.position;
        animation_.start_orientation = glm::vec3(heading_, pitch_, roll_);
        animation_.target_orientation = glm::vec3(target_heading, target_pitch, 0.0f);
        animation_.duration = duration_seconds;
        animation_.elapsed = 0.0f;
        animation_.active = true;
        animation_.easing_function = Easing::EaseInOutCubic;
    }

    void LookAt(const glm::vec3& target) override {
        // Calculate direction from position to target
        glm::vec3 direction = glm::normalize(target - position_);

        CameraState state = GetCurrentState();

        // Calculate heading (yaw) from x,z components
        state.heading = glm::degrees(std::atan2(direction.x, direction.z));

        // Calculate pitch from y component
        float clamped_y = std::clamp(direction.y, -1.0f, 1.0f);
        state.pitch = glm::degrees(std::asin(clamped_y));

        ApplyState(state);
        SetFromState(state);

        // In ORBIT mode, also set the target
        if (movement_mode_ == MovementMode::ORBIT) {
            target_ = target;
        }

        UpdateViewMatrix();
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

    // Key held state tracking for continuous WASD movement
    std::set<int> held_keys_;
    
    // Matrices
    glm::mat4 view_matrix_ = glm::mat4(1.0f);
    
    /**
     * @brief Single enforcement point for all camera constraints.
     *
     * Validates and clamps the given state to respect:
     * - Distance from origin: [MIN_DISTANCE_NORMALIZED, MAX_DISTANCE_NORMALIZED]
     * - Pitch: [-89°, 89°]
     * - Heading/roll: normalized to [0°, 360°)
     *
     * @param state Camera state to validate (modified in place)
     * @return true if state was clamped (constraint was hit)
     */
    bool ApplyState(CameraState& state) {
        bool clamped = false;

        // Enforce distance constraints
        const float min_distance = constants::camera_constraints::MIN_DISTANCE_NORMALIZED;
        const float max_distance = constants::camera_constraints::MAX_DISTANCE_NORMALIZED;
        const float distance = glm::length(state.position);

        if (distance < min_distance) {
            state.position = glm::normalize(state.position) * min_distance;
            clamped = true;
        } else if (distance > max_distance) {
            state.position = glm::normalize(state.position) * max_distance;
            clamped = true;
        }

        // Handle zero-length position (shouldn't happen, but defensive)
        if (distance < 0.0001f) {
            state.position = glm::vec3(0.0f, 0.0f, min_distance);
            clamped = true;
        }

        // Enforce pitch constraints
        const float min_pitch = constraints_.min_pitch;
        const float max_pitch = constraints_.max_pitch;
        if (state.pitch < min_pitch) {
            state.pitch = min_pitch;
            clamped = true;
        } else if (state.pitch > max_pitch) {
            state.pitch = max_pitch;
            clamped = true;
        }

        // Normalize heading to [0°, 360°)
        state.heading = std::fmod(state.heading, 360.0f);
        if (state.heading < 0.0f) {
            state.heading += 360.0f;
        }

        // Normalize roll to [0°, 360°)
        state.roll = std::fmod(state.roll, 360.0f);
        if (state.roll < 0.0f) {
            state.roll += 360.0f;
        }

        return clamped;
    }

    /**
     * @brief Get current state as CameraState struct
     */
    CameraState GetCurrentState() const {
        return CameraState{position_, heading_, pitch_, roll_};
    }

    /**
     * @brief Apply validated state to internal members
     */
    void SetFromState(const CameraState& state) {
        position_ = state.position;
        heading_ = state.heading;
        pitch_ = state.pitch;
        roll_ = state.roll;
    }

    /**
     * @brief Update clipping planes based on altitude.
     *
     * Near plane must be smaller than distance-to-surface, otherwise
     * the globe gets clipped. We use 10% of altitude as near plane.
     */
    void UpdateClippingPlanes() {
        float altitude = glm::length(position_) - 1.0f;

        // Near plane = 10% of altitude, with minimum to avoid precision issues
        float adaptive_near = std::max(
            altitude * 0.1f,
            constants::camera::MIN_NEAR_PLANE_NORMALIZED
        );

        // Far plane stays large
        near_plane_ = adaptive_near;
        // far_plane_ unchanged
    }

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

        CameraState state;

        if (animation_.IsComplete()) {
            // Apply final values
            state.position = animation_.target_position;
            state.heading = animation_.target_orientation.x;
            state.pitch = animation_.target_orientation.y;
            state.roll = animation_.target_orientation.z;
            animation_.Reset();
        } else {
            // Apply interpolated values
            float progress = animation_.GetProgress();
            float eased_progress = animation_.easing_function(progress);

            state.position = glm::mix(animation_.start_position, animation_.target_position, eased_progress);

            glm::vec3 current_orientation = glm::mix(animation_.start_orientation,
                                                   animation_.target_orientation,
                                                   eased_progress);
            state.heading = current_orientation.x;
            state.pitch = current_orientation.y;
            state.roll = current_orientation.z;
        }

        // Enforce constraints on every animation frame
        ApplyState(state);
        SetFromState(state);
    }
    
    void UpdateMovement(float delta_time) {
        if (movement_mode_ != MovementMode::FREE) {
            return;
        }

        glm::vec3 forward = GetForwardVector();
        glm::vec3 right = GetRightVector();
        glm::vec3 up_vec = GetUpVector();

        // Altitude-proportional speed (in normalized units directly).
        // At altitude 1.0, move 2.0 units/sec (fast traversal)
        // At altitude 0.01, move 0.02 units/sec (precise control near surface)
        // Minimum speed ensures movement is always visible
        float altitude = glm::length(position_) - 1.0f;
        float speed = std::max(altitude, 0.001f) * 2.0f;

        float rot_speed = constraints_.max_rotation_speed;

        // Build new state from current + deltas
        CameraState state = GetCurrentState();

        // Update position
        if (glm::abs(movement_forward_) > 0.01f) {
            state.position += forward * movement_forward_ * speed * delta_time;
        }
        if (glm::abs(movement_right_) > 0.01f) {
            state.position += right * movement_right_ * speed * delta_time;
        }
        if (glm::abs(movement_up_) > 0.01f) {
            state.position += up_vec * movement_up_ * speed * delta_time;
        }

        // Update orientation
        if (glm::abs(rotation_x_) > 0.01f) {
            state.heading += rotation_x_ * rot_speed * delta_time;
        }
        if (glm::abs(rotation_y_) > 0.01f) {
            state.pitch += rotation_y_ * rot_speed * delta_time;
        }
        if (glm::abs(rotation_z_) > 0.01f) {
            state.roll += rotation_z_ * rot_speed * delta_time;
        }

        // Enforce all constraints via single enforcement point
        ApplyState(state);
        SetFromState(state);

        // Apply decay to movement impulses only when keys are NOT held.
        // This allows continuous movement while holding WASD, but smooth
        // stopping with momentum when released.
        float decay_rate = 10.0f;  // Higher = faster decay

        // Check if forward/backward keys are held
        bool forward_held = held_keys_.count('W') || held_keys_.count('S') ||
                           held_keys_.count(265) || held_keys_.count(264);
        if (!forward_held) {
            movement_forward_ *= std::exp(-decay_rate * delta_time);
            if (std::abs(movement_forward_) < 0.001f) movement_forward_ = 0.0f;
        }

        // Check if left/right keys are held
        bool right_held = held_keys_.count('A') || held_keys_.count('D') ||
                         held_keys_.count(263) || held_keys_.count(262);
        if (!right_held) {
            movement_right_ *= std::exp(-decay_rate * delta_time);
            if (std::abs(movement_right_) < 0.001f) movement_right_ = 0.0f;
        }

        // Check if up/down keys are held
        bool up_held = held_keys_.count('Q') || held_keys_.count('E');
        if (!up_held) {
            movement_up_ *= std::exp(-decay_rate * delta_time);
            if (std::abs(movement_up_) < 0.001f) movement_up_ = 0.0f;
        }

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

        // Altitude-proportional sensitivity: precise at low altitude, responsive at high
        float altitude = glm::length(position_) - 1.0f;
        float altitude_factor = std::max(altitude, 0.001f);

        CameraState state = GetCurrentState();

        // Middle mouse button: Tilt and rotate camera (pitch and heading)
        if (middle_mouse_dragging_ && active_mouse_button_ == 2) {
            if (movement_mode_ == MovementMode::ORBIT) {
                // Tilt mode: change pitch (up/down drag) and heading (left/right drag)
                float sensitivity = altitude_factor * 0.3f;

                state.heading -= delta.x * sensitivity;
                state.pitch -= delta.y * sensitivity;

                // Apply constraints
                ApplyState(state);
                SetFromState(state);

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

                // Apply position with constraints
                state = GetCurrentState();
                state.position = target_ + offset;
                ApplyState(state);
                SetFromState(state);
            } else {
                // Free mode: adjust pitch and heading via Rotate
                float sensitivity = altitude_factor * 0.2f;
                Rotate(-delta.x * sensitivity, -delta.y * sensitivity);
            }
        }
        // Left mouse button: Standard orbit/rotation
        else if (mouse_dragging_ && active_mouse_button_ == 0) {
            if (movement_mode_ == MovementMode::ORBIT) {
                // Orbital camera controls
                float sensitivity = altitude_factor * 0.1f;

                // Rotate around target
                glm::vec3 offset = position_ - target_;

                // Horizontal rotation (around Y axis)
                glm::quat horiz_rot = glm::angleAxis(-delta.x * sensitivity * 0.01f, glm::vec3(0.0f, 1.0f, 0.0f));
                offset = horiz_rot * offset;

                // Vertical rotation (around right vector)
                glm::vec3 right = glm::normalize(glm::cross(glm::normalize(offset), glm::vec3(0.0f, 1.0f, 0.0f)));
                glm::quat vert_rot = glm::angleAxis(delta.y * sensitivity * 0.01f, right);
                offset = vert_rot * offset;

                // Apply with constraints
                state.position = target_ + offset;
                ApplyState(state);
                SetFromState(state);
            } else {
                // Free camera controls - use rotation impulses
                float sensitivity = altitude_factor * 0.2f;
                rotation_x_ = -delta.x * sensitivity;
                rotation_y_ = -delta.y * sensitivity;
            }
        }

        UpdateViewMatrix();
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
        // Altitude-proportional zoom: step is a fraction of remaining altitude.
        // This ensures smooth zoom at all altitudes without overshooting surface.
        // At high altitude: large steps (responsive)
        // At low altitude: small steps (precise, never overshoots)

        float distance = glm::length(position_);
        float altitude = distance - 1.0f;  // Height above surface (normalized units)

        // Zoom step is 30% of remaining altitude
        constexpr float kZoomFraction = 0.3f;
        float zoom_step = altitude * kZoomFraction;

        // Positive scroll = zoom in (reduce altitude)
        float new_altitude = altitude - event.scroll_delta * zoom_step;

        // Clamp to valid range
        constexpr float kMinAltitude = constants::camera_constraints::MIN_DISTANCE_NORMALIZED - 1.0f;
        constexpr float kMaxAltitude = constants::camera_constraints::MAX_DISTANCE_NORMALIZED - 1.0f;
        new_altitude = std::clamp(new_altitude, kMinAltitude, kMaxAltitude);

        float new_distance = 1.0f + new_altitude;

        CameraState state = GetCurrentState();
        state.position = glm::normalize(state.position) * new_distance;
        SetFromState(state);
        UpdateClippingPlanes();
        UpdateViewMatrix();
        return true;
    }

    bool HandleKeyPress(const InputEvent& event) {
        // Track held state for continuous movement
        held_keys_.insert(event.key);

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
        // Clear held state
        held_keys_.erase(event.key);

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

            // Google Earth style: fly camera to position ABOVE the clicked point
            // while keeping target_ at origin (globe center) for consistent orbit behavior
            if (movement_mode_ == MovementMode::ORBIT) {
                // Convert hit point to spherical coordinates (lat/lon on normalized sphere)
                // hit_point is on sphere with radius 1.0
                float lat = std::asin(std::clamp(hit_point.y, -1.0f, 1.0f));  // -π/2 to π/2
                float lon = std::atan2(hit_point.x, hit_point.z);  // -π to π

                // Calculate new camera position ABOVE this point
                float current_altitude = glm::length(position_) - 1.0f;  // Current altitude above surface
                float target_altitude = std::max(current_altitude * 0.5f, 0.02f);  // Zoom in, min ~130km
                float new_distance = 1.0f + target_altitude;

                // Position camera directly above the clicked point (radially outward)
                glm::vec3 new_position;
                new_position.x = new_distance * std::cos(lat) * std::sin(lon);
                new_position.y = new_distance * std::sin(lat);
                new_position.z = new_distance * std::cos(lat) * std::cos(lon);

                // IMPORTANT: target_ stays at origin for consistent constraint enforcement!
                // Calculate new orientation to look at globe center
                glm::vec3 direction = glm::normalize(-new_position);  // Look toward origin
                float new_heading = glm::degrees(std::atan2(direction.x, direction.z));
                float new_pitch = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
                new_pitch = std::clamp(new_pitch, constraints_.min_pitch, constraints_.max_pitch);

                // Animate to new position and orientation
                animation_.start_position = position_;
                animation_.target_position = new_position;
                animation_.start_orientation = glm::vec3(heading_, pitch_, roll_);
                animation_.target_orientation = glm::vec3(new_heading, new_pitch, 0.0f);
                animation_.duration = 0.8f;
                animation_.elapsed = 0.0f;
                animation_.active = true;
                animation_.easing_function = Easing::EaseInOutCubic;

                // target_ remains at origin - do NOT change it!

                spdlog::info("Double-click: flying above ({:.2f}°, {:.2f}°) at altitude {:.4f}",
                           glm::degrees(lat), glm::degrees(lon), target_altitude);
            }
        } else {
            // Ray missed the globe - just zoom in toward globe center
            if (movement_mode_ == MovementMode::ORBIT) {
                float current_distance = glm::length(position_);
                float new_distance = current_distance * 0.5f;

                // Apply min distance constraint
                new_distance = std::max(new_distance, constants::camera_constraints::MIN_DISTANCE_NORMALIZED);

                glm::vec3 new_position = glm::normalize(position_) * new_distance;

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
