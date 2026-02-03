#pragma once

/**
 * @file camera.h
 * @brief Camera system for 3D globe rendering
 * 
 * Provides abstract camera interface and concrete implementations
 * for different camera types (perspective, orthographic) with
 * full support for globe navigation and interaction.
 */

#include <earth_map/math/frustum.h>
#include <earth_map/math/bounding_box.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <cstdint>

namespace earth_map {

// Forward declarations
struct Configuration;

/**
 * @brief Camera projection types
 */
enum class CameraProjectionType {
    PERSPECTIVE,    ///< Perspective projection (3D)
    ORTHOGRAPHIC    ///< Orthographic projection (2D)
};

/**
 * @brief Camera movement modes
 */
enum class MovementMode {
    FREE,          ///< Free camera movement (6DOF)
    ORBIT,         ///< Orbit around target point
    FOLLOW_TERRAIN, ///< Follow terrain elevation
    FIRST_PERSON   ///< First-person ground level view
};

/**
 * @brief Camera animation states
 */
enum class AnimationState {
    IDLE,          ///< No active animation
    MOVING,        ///< Position animation
    ROTATING,      ///< Rotation animation
    ZOOMING        ///< Zoom animation
};

/**
 * @brief Camera constraints structure
 */
struct CameraConstraints {
    float min_altitude = 100.0f;          ///< Minimum altitude above surface (meters)
    float max_altitude = 10000000.0f;     ///< Maximum altitude above surface (meters)
    float min_pitch = -89.0f;             ///< Minimum pitch angle (degrees)
    float max_pitch = 89.0f;              ///< Maximum pitch angle (degrees)
    bool enable_ground_collision = true;  ///< Enable ground collision detection
    float ground_clearance = 10.0f;        ///< Minimum clearance above ground (meters)
    float max_rotation_speed = 180.0f;    ///< Maximum rotation speed (degrees/second)
    float max_movement_speed = 1000.0f;   ///< Maximum movement speed (meters/second)
};

/**
 * @brief Input event structure for camera interaction
 */
struct InputEvent {
    enum class Type {
        MOUSE_MOVE,
        MOUSE_BUTTON_PRESS,
        MOUSE_BUTTON_RELEASE,
        MOUSE_SCROLL,
        KEY_PRESS,
        KEY_RELEASE,
        TOUCH_START,
        TOUCH_MOVE,
        TOUCH_END,
        DOUBLE_CLICK
    };
    
    Type type;
    float x = 0.0f;
    float y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    int button = 0;
    int key = 0;
    float scroll_delta = 0.0f;
    uint64_t timestamp = 0;
    
    // Touch-specific fields
    int pointer_id = 0;
    float x2 = 0.0f;
    float y2 = 0.0f;
};

/**
 * @brief Abstract camera interface
 * 
 * Defines the common interface for all camera implementations.
 * Provides methods for position control, projection management,
 * and view matrix calculation.
 */
class Camera {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~Camera() = default;
    
    /**
     * @brief Initialize the camera
     * 
     * Sets up internal camera state and performs any necessary
     * initialization tasks.
     * 
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize() = 0;
    
    /**
     * @brief Update camera state
     * 
     * Updates camera animations, processes input, and recalculates
     * internal matrices. Should be called once per frame.
     * 
     * @param delta_time Time since last update in seconds
     */
    virtual void Update(float delta_time) = 0;
    
    /**
     * @brief Set camera position in geographic coordinates
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters above sea level
     */
    virtual void SetGeographicPosition(double longitude, double latitude, double altitude) = 0;
    
    /**
     * @brief Set camera position in Cartesian coordinates
     * 
     * @param position 3D position in world space
     */
    virtual void SetPosition(const glm::vec3& position) = 0;
    
    /**
     * @brief Get camera position
     * 
     * @return glm::vec3 Current camera position in world space
     */
    virtual glm::vec3 GetPosition() const = 0;
    
    /**
     * @brief Set camera target in geographic coordinates
     * 
     * @param longitude Target longitude in degrees
     * @param latitude Target latitude in degrees
     * @param altitude Target altitude in meters
     */
    virtual void SetGeographicTarget(double longitude, double latitude, double altitude) = 0;
    
    /**
     * @brief Set camera target
     * 
     * @param target Target point in world space
     */
    virtual void SetTarget(const glm::vec3& target) = 0;
    
    /**
     * @brief Get camera target
     * 
     * @return glm::vec3 Current camera target in world space
     */
    virtual glm::vec3 GetTarget() const = 0;
    
    /**
     * @brief Set camera orientation
     * 
     * @param heading Heading angle in degrees (0-360)
     * @param pitch Pitch angle in degrees (-90 to 90)
     * @param roll Roll angle in degrees (-180 to 180)
     */
    virtual void SetOrientation(double heading, double pitch, double roll) = 0;
    
    /**
     * @brief Get camera orientation
     * 
     * @return glm::vec3 (heading, pitch, roll) in degrees
     */
    virtual glm::vec3 GetOrientation() const = 0;
    
    /**
     * @brief Set field of view
     * 
     * @param fov_y Vertical field of view in degrees (1-179)
     */
    virtual void SetFieldOfView(float fov_y) = 0;
    
    /**
     * @brief Get field of view
     * 
     * @return float Vertical field of view in degrees
     */
    virtual float GetFieldOfView() const = 0;
    
    /**
     * @brief Set near and far clipping planes
     * 
     * @param near_plane Near clipping distance (> 0)
     * @param far_plane Far clipping distance (> near_plane)
     */
    virtual void SetClippingPlanes(float near_plane, float far_plane) = 0;

    /**
     * @brief Get near clipping plane distance
     *
     * @return float Near clipping distance
     */
    virtual float GetNearPlane() const = 0;

    /**
     * @brief Get far clipping plane distance
     *
     * @return float Far clipping distance
     */
    virtual float GetFarPlane() const = 0;
    
    /**
     * @brief Get view matrix
     * 
     * @return glm::mat4 View matrix for rendering
     */
    virtual glm::mat4 GetViewMatrix() const = 0;
    
    /**
     * @brief Get projection matrix
     * 
     * @param aspect_ratio Viewport aspect ratio (width/height)
     * @return glm::mat4 Projection matrix for rendering
     */
    virtual glm::mat4 GetProjectionMatrix(float aspect_ratio) const = 0;
    
    /**
     * @brief Get view-projection matrix
     * 
     * @param aspect_ratio Viewport aspect ratio (width/height)
     * @return glm::mat4 Combined view-projection matrix
     */
    virtual glm::mat4 GetViewProjectionMatrix(float aspect_ratio) const = 0;
    
    /**
     * @brief Get view frustum
     * 
     * @param aspect_ratio Viewport aspect ratio (width/height)
     * @return Frustum Current camera frustum for culling
     */
    virtual Frustum GetFrustum(float aspect_ratio) const = 0;
    
    /**
     * @brief Set projection type
     * 
     * @param projection_type Camera projection type
     */
    virtual void SetProjectionType(CameraProjectionType projection_type) = 0;
    
    /**
     * @brief Get projection type
     * 
     * @return CameraProjectionType Current projection type
     */
    virtual CameraProjectionType GetProjectionType() const = 0;
    
    /**
     * @brief Set movement mode
     * 
     * @param movement_mode Camera movement mode
     */
    virtual void SetMovementMode(MovementMode movement_mode) = 0;
    
    /**
     * @brief Get movement mode
     * 
     * @return MovementMode Current movement mode
     */
    virtual MovementMode GetMovementMode() const = 0;
    
    /**
     * @brief Set camera constraints
     * 
     * @param constraints Camera movement and position constraints
     */
    virtual void SetConstraints(const CameraConstraints& constraints) = 0;
    
    /**
     * @brief Get camera constraints
     * 
     * @return CameraConstraints Current camera constraints
     */
    virtual CameraConstraints GetConstraints() const = 0;
    
    /**
     * @brief Process input event
     * 
     * Processes mouse, keyboard, and touch input for camera control.
     * 
     * @param event Input event to process
     * @return true if event was handled, false otherwise
     */
    virtual bool ProcessInput(const InputEvent& event) = 0;
    
    /**
     * @brief Get current animation state
     * 
     * @return AnimationState Current animation state
     */
    virtual AnimationState GetAnimationState() const = 0;
    
    /**
     * @brief Check if camera is animating
     * 
     * @return true if camera has active animations, false otherwise
     */
    virtual bool IsAnimating() const = 0;
    
    /**
     * @brief Reset camera to default position
     * 
     * Resets camera to default globe view position and orientation.
     */
    virtual void Reset() = 0;
    
    /**
     * @brief Animate camera to new position
     * 
     * Smoothly animates camera to new geographic position over specified duration.
     * 
     * @param longitude Target longitude in degrees
     * @param latitude Target latitude in degrees
     * @param altitude Target altitude in meters
     * @param duration Animation duration in seconds
     */
    virtual void AnimateToGeographic(double longitude, double latitude, double altitude, float duration) = 0;
    
    /**
     * @brief Animate camera to new orientation
     * 
     * Smoothly animates camera to new orientation over specified duration.
     * 
     * @param heading Target heading in degrees
     * @param pitch Target pitch in degrees
     * @param roll Target roll in degrees
     * @param duration Animation duration in seconds
     */
    virtual void AnimateToOrientation(double heading, double pitch, double roll, float duration) = 0;
    
    /**
     * @brief Stop all animations
     *
     * Immediately stops all camera animations and holds current state.
     */
    virtual void StopAnimations() = 0;

    // =========================================================================
    // High-Level Camera Control API
    // =========================================================================

    /**
     * @brief Zoom camera by multiplicative factor
     *
     * Multiplicative zoom that works consistently at all distances.
     * Each call scales the camera distance from globe center by the given factor.
     *
     * @param factor Zoom factor. Values < 1.0 zoom in (closer), > 1.0 zoom out (farther).
     *               E.g., 0.9 = 10% closer, 1.1 = 10% farther.
     *
     * @note Distance is automatically clamped to [MIN_DISTANCE, MAX_DISTANCE].
     *       This method never causes stuck states or violates constraints.
     */
    virtual void Zoom(float factor) = 0;

    /**
     * @brief Pan camera by screen-space offset
     *
     * Moves the camera view by the given screen-space delta.
     * In ORBIT mode: rotates the view around the globe.
     * In FREE mode: translates the camera position.
     *
     * @param screen_dx Horizontal offset (positive = pan right)
     * @param screen_dy Vertical offset (positive = pan down)
     *
     * @note All constraints are enforced. Pan at minimum altitude will
     *       be clamped to not penetrate the globe surface.
     */
    virtual void Pan(float screen_dx, float screen_dy) = 0;

    /**
     * @brief Rotate camera orientation by delta angles
     *
     * Applies incremental rotation to camera heading and pitch.
     *
     * @param delta_heading Heading change in degrees (positive = turn right)
     * @param delta_pitch Pitch change in degrees (positive = look up)
     *
     * @note Pitch is automatically clamped to [-89°, 89°].
     *       Heading wraps around at 360°.
     */
    virtual void Rotate(float delta_heading, float delta_pitch) = 0;

    /**
     * @brief Animated flight to geographic location
     *
     * Smoothly animates the camera to the specified geographic position
     * over the given duration using eased interpolation.
     *
     * @param longitude Target longitude in degrees (-180 to 180)
     * @param latitude Target latitude in degrees (-90 to 90)
     * @param altitude_meters Target altitude in meters above sea level
     * @param duration_seconds Animation duration in seconds (default: 2.0)
     *
     * @note Altitude is clamped to [MIN_ALTITUDE, MAX_ALTITUDE] constraints.
     *       The animation path may be adjusted to respect constraints.
     */
    virtual void FlyTo(double longitude, double latitude, double altitude_meters,
                       float duration_seconds = 2.0f) = 0;

    /**
     * @brief Point camera at target location
     *
     * Orients the camera to look at the specified world-space position
     * without changing the camera's position.
     *
     * @param target Target point in world space to look at
     *
     * @note In ORBIT mode, this also sets the orbit center.
     *       Pitch is clamped to constraints.
     */
    virtual void LookAt(const glm::vec3& target) = 0;

    /**
     * @brief Get forward vector
     * 
     * @return glm::vec3 Normalized forward direction vector
     */
    virtual glm::vec3 GetForwardVector() const = 0;
    
    /**
     * @brief Get right vector
     * 
     * @return glm::vec3 Normalized right direction vector
     */
    virtual glm::vec3 GetRightVector() const = 0;
    
    /**
     * @brief Get up vector
     * 
     * @return glm::vec3 Normalized up direction vector
     */
    virtual glm::vec3 GetUpVector() const = 0;
    
    /**
     * @brief Screen ray casting
     * 
     * Casts a ray from camera through screen coordinates.
     * 
     * @param screen_x Screen X coordinate (0 to 1)
     * @param screen_y Screen Y coordinate (0 to 1)
     * @param aspect_ratio Viewport aspect ratio
     * @return glm::vec3 Ray direction vector (normalized)
     */
    virtual glm::vec3 ScreenToWorldRay(float screen_x, float screen_y, float aspect_ratio) const = 0;

protected:
    /**
     * @brief Protected constructor
     */
    Camera() = default;
};

/**
 * @brief Factory function to create perspective camera
 * 
 * @param config Configuration parameters
 * @return std::unique_ptr<Camera> New perspective camera instance
 */
std::unique_ptr<Camera> CreatePerspectiveCamera(const Configuration& config);

/**
 * @brief Factory function to create orthographic camera
 * 
 * @param config Configuration parameters
 * @return std::unique_ptr<Camera> New orthographic camera instance
 */
std::unique_ptr<Camera> CreateOrthographicCamera(const Configuration& config);

} // namespace earth_map
