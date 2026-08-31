#pragma once

/**
 * @file camera_controller.h
 * @brief Camera control and viewport management
 * 
 * Provides camera controls for 3D globe navigation including
 * position, orientation, and projection management.
 */

#include "earth_map/math/frustum.h"
#include <earth_map/math/bounding_box.h>
#include <earth_map/geodesy/wgs84_ellipsoid.h>
#include "earth_map/renderer/camera.h"
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>
#include <optional>
#include <utility>

namespace earth_map {

// Forward declarations
struct Configuration;

/**
 * @brief Camera controller for 3D globe navigation
 * 
 * Manages camera position, orientation, and projection for
 * navigating the 3D globe scene. Supports both geographic
 * coordinates and Cartesian coordinates.
 */
class CameraController {
public:   
    /**
     * @brief Camera movement modes
     */
    enum class MovementMode {
        FREE,          ///< Free camera movement
        ORBIT,         ///< Orbit around target
        FOLLOW_TERRAIN  ///< Follow terrain elevation
    };
    
    /**
     * @brief Virtual destructor
     */
    virtual ~CameraController() = default;
    
    /**
     * @brief Initialize the camera controller
     * 
     * Sets up initial camera state and internal data structures
     * 
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize() = 0;
    
    /**
     * @brief Set camera position in geographic coordinates
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     */
    virtual void SetGeographicPosition(double longitude, double latitude, double altitude) = 0;
    
    virtual void SetEcefPosition(const geodesy::EcefPosition& position) = 0;
    [[nodiscard]] virtual geodesy::EcefPosition GetEcefPosition() const = 0;
    
    /**
     * @brief Set camera target in geographic coordinates
     * 
     * @param longitude Target longitude in degrees
     * @param latitude Target latitude in degrees
     * @param altitude Target altitude in meters
     */
    virtual void SetGeographicTarget(double longitude, double latitude, double altitude) = 0;
    
    virtual void SetEcefTarget(const geodesy::EcefPosition& target) = 0;
    [[nodiscard]] virtual geodesy::EcefPosition GetEcefTarget() const = 0;

    /**
     * Casts a screen ray in the physical WGS84/ECEF world.
     * Screen coordinates are normalized to [0, 1].
     */
    [[nodiscard]] virtual std::pair<geodesy::EcefPosition, glm::dvec3> ScreenToEcefRay(
        float screen_x, float screen_y, float aspect_ratio) const = 0;

    /**
     * Projects a physical WGS84/ECEF position to normalized [0, 1] screen
     * coordinates using the current view/projection (the inverse of
     * ScreenToEcefRay for a single point). Returns nullopt if the position
     * is behind the camera.
     */
    [[nodiscard]] virtual std::optional<glm::vec2> EcefToScreen(
        const geodesy::EcefPosition& position, float aspect_ratio) const = 0;
    
    /**
     * @brief Set camera orientation
     * 
     * @param heading Heading angle in degrees
     * @param pitch Pitch angle in degrees (-90 to 90)
     * @param roll Roll angle in degrees
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
     * @param fov_y Vertical field of view in degrees
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
      * @param near_plane Near clipping distance
      * @param far_plane Far clipping distance
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
      * @brief Get camera forward vector
      *
      * @return Normalized forward direction in the camera-relative ENU render frame
      */
     virtual glm::vec3 GetForwardVector() const = 0;
    
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
     * @brief Update camera animation
     *
     * @param delta_time Time since last update in seconds
     */
    virtual void Update(float delta_time) = 0;

    /**
     * @brief Reset camera to default position
     *
     * Resets camera to default globe view position
     */
    virtual void Reset() = 0;

    /**
     * @brief Process input event
     *
     * @param event Input event to process
     * @return true if event was handled, false otherwise
     */
    virtual bool ProcessInput(const InputEvent& event) = 0;

    // =========================================================================
    // High-Level Camera Control API
    // =========================================================================

    /**
     * @brief Zoom camera by multiplicative factor
     *
     * @param factor Zoom factor. < 1.0 = zoom in, > 1.0 = zoom out.
     * @see Camera::Zoom for detailed documentation
     */
    virtual void Zoom(float factor) = 0;

    /**
     * @brief Pan camera by screen-space offset
     *
     * @param screen_dx Horizontal offset
     * @param screen_dy Vertical offset
     * @see Camera::Pan for detailed documentation
     */
    virtual void Pan(float screen_dx, float screen_dy) = 0;

    /**
     * @brief Rotate camera orientation by delta angles
     *
     * @param delta_heading Heading change in degrees
     * @param delta_pitch Pitch change in degrees
     * @see Camera::Rotate for detailed documentation
     */
    virtual void Rotate(float delta_heading, float delta_pitch) = 0;

    /**
     * @brief Animated flight to geographic location
     *
     * @param longitude Target longitude in degrees
     * @param latitude Target latitude in degrees
     * @param altitude_meters Target altitude in meters
     * @param duration_seconds Animation duration (default: 2.0)
     * @see Camera::FlyTo for detailed documentation
     */
    virtual void FlyTo(double longitude, double latitude, double altitude_meters,
                       float duration_seconds = 2.0f) = 0;

    /**
     * @brief Point camera at target location
     *
     * @param target Target point in world space
     * @see Camera::LookAt for detailed documentation
     */
    virtual void LookAt(const geodesy::EcefPosition& target) = 0;

protected:
    /**
     * @brief Protected constructor
     */
    CameraController() = default;
};

/**
 * @brief Factory function to create camera controller instance
 * 
 * @param config Configuration parameters
 * @return CameraController* New camera controller instance (caller owns)
 */
CameraController* CreateCameraController(const Configuration& config);

} // namespace earth_map
