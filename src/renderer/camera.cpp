#include <earth_map/renderer/camera.h>

#include "ecef_render_frame.h"

#include <earth_map/earth_map.h>

// glm::gtx is an experimental GLM module; this define is GLM's own required
// opt-in, not a project convention -- must precede any glm/gtx include.
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace earth_map {
namespace {

constexpr double kMinimumLookDistanceMeters = 1.0;
constexpr double kDefaultCameraAltitudeMeters =
    geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters * 1.5;

[[nodiscard]] double DegreesToRadians(const double degrees) noexcept {
    return degrees * glm::pi<double>() / 180.0;
}

[[nodiscard]] double RadiansToDegrees(const double radians) noexcept {
    return radians * 180.0 / glm::pi<double>();
}

[[nodiscard]] double NormalizeDegrees(double degrees) noexcept {
    degrees = std::fmod(degrees, 360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

[[nodiscard]] double ShortestLongitudeDelta(const double from, const double to) noexcept {
    double delta = to - from;
    while (delta > glm::pi<double>()) {
        delta -= glm::two_pi<double>();
    }
    while (delta < -glm::pi<double>()) {
        delta += glm::two_pi<double>();
    }
    return delta;
}

[[nodiscard]] geodesy::GeodeticPosition GeographicDegrees(
    const double longitude,
    const double latitude,
    const double height_meters) noexcept {
    return {
        .latitude_radians = std::clamp(DegreesToRadians(latitude), -glm::half_pi<double>(), glm::half_pi<double>()),
        .longitude_radians = DegreesToRadians(longitude),
        .ellipsoid_height_meters = height_meters,
    };
}

[[nodiscard]] glm::dvec3 NormalizedOr(const glm::dvec3& value, const glm::dvec3& fallback) noexcept {
    const double length = glm::length(value);
    return length > std::numeric_limits<double>::epsilon() ? value / length : fallback;
}

[[nodiscard]] glm::dvec3 LocalDirection(const double heading_degrees, const double pitch_degrees) noexcept {
    const double heading = DegreesToRadians(heading_degrees);
    const double pitch = DegreesToRadians(pitch_degrees);
    return {
        std::sin(heading) * std::cos(pitch),
        std::cos(heading) * std::cos(pitch),
        std::sin(pitch),
    };
}

[[nodiscard]] float EaseInOutCubic(const float value) noexcept {
    return value < 0.5f
        ? 4.0f * value * value * value
        : 1.0f - 4.0f * std::pow(1.0f - value, 3.0f);
}

struct CameraAnimation final {
    bool active = false;
    float duration_seconds = 0.0f;
    float elapsed_seconds = 0.0f;
    geodesy::GeodeticPosition start_camera;
    geodesy::GeodeticPosition target_camera;
    geodesy::GeodeticPosition start_target;
    geodesy::GeodeticPosition target_target;
    glm::dvec3 start_orientation{0.0};
    glm::dvec3 target_orientation{0.0};
    bool animate_position = false;
    bool animate_orientation = false;

    void Reset() noexcept {
        active = false;
        duration_seconds = 0.0f;
        elapsed_seconds = 0.0f;
        animate_position = false;
        animate_orientation = false;
    }
};

class CameraImpl : public Camera {
public:
    explicit CameraImpl(const Configuration& config)
        : config_(config) {
        Reset();
    }

    bool Initialize() override {
        initialized_ = true;
        return true;
    }

    void Update(const float delta_time) override {
        UpdateAnimation(std::max(delta_time, 0.0f));
        UpdateMovement(std::max(delta_time, 0.0f));
        UpdateClippingPlanes();
        UpdateViewMatrix();
    }

    void SetGeographicPosition(
        const double longitude,
        const double latitude,
        const double altitude) override {
        const geodesy::GeodeticPosition camera = GeographicDegrees(
            longitude, latitude, ClampHeight(altitude));
        position_ = geodesy::Wgs84Ellipsoid::ToEcef(camera);
        target_ = geodesy::Wgs84Ellipsoid::ToEcef({
            .latitude_radians = camera.latitude_radians,
            .longitude_radians = camera.longitude_radians,
            .ellipsoid_height_meters = 0.0,
        });
        UpdateOrientationFromTarget();
        UpdateViewMatrix();
    }

    void SetEcefPosition(const geodesy::EcefPosition& position) override {
        position_ = ClampPositionHeight(position);
        EnsureValidTarget();
        UpdateOrientationFromTarget();
        UpdateViewMatrix();
    }

    [[nodiscard]] geodesy::EcefPosition GetEcefPosition() const override {
        return position_;
    }

    void SetGeographicTarget(
        const double longitude,
        const double latitude,
        const double altitude) override {
        SetEcefTarget(geodesy::Wgs84Ellipsoid::ToEcef(
            GeographicDegrees(longitude, latitude, altitude)));
    }

    void SetEcefTarget(const geodesy::EcefPosition& target) override {
        target_ = target;
        EnsureValidTarget();
        UpdateOrientationFromTarget();
        UpdateViewMatrix();
    }

    [[nodiscard]] geodesy::EcefPosition GetEcefTarget() const override {
        return target_;
    }

    void SetOrientation(const double heading, const double pitch, const double roll) override {
        heading_degrees_ = NormalizeDegrees(heading);
        pitch_degrees_ = std::clamp(pitch,
                                    static_cast<double>(constraints_.min_pitch),
                                    static_cast<double>(constraints_.max_pitch));
        roll_degrees_ = std::clamp(roll, -180.0, 180.0);

        const renderer::EcefRenderFrame camera_frame = CurrentRenderFrame();
        const double look_distance = std::max(
            glm::length(position_.meters - target_.meters), kMinimumLookDistanceMeters);
        target_ = camera_frame.FromLocal(
            LocalDirection(heading_degrees_, pitch_degrees_) * look_distance);
        UpdateViewMatrix();
    }

    [[nodiscard]] glm::vec3 GetOrientation() const override {
        return glm::vec3(static_cast<float>(heading_degrees_),
                         static_cast<float>(pitch_degrees_),
                         static_cast<float>(roll_degrees_));
    }

    void SetFieldOfView(const float fov_y) override {
        fov_y_ = std::clamp(fov_y, 1.0f, 179.0f);
    }

    [[nodiscard]] float GetFieldOfView() const override {
        return fov_y_;
    }

    void SetClippingPlanes(const float near_plane, const float far_plane) override {
        near_plane_ = std::max(near_plane, 0.01f);
        far_plane_ = std::max(far_plane, near_plane_ + 1.0f);
        clipping_planes_explicit_ = true;
    }

    [[nodiscard]] float GetNearPlane() const override {
        return near_plane_;
    }

    [[nodiscard]] float GetFarPlane() const override {
        return far_plane_;
    }

    [[nodiscard]] glm::mat4 GetViewMatrix() const override {
        return view_matrix_;
    }

    [[nodiscard]] glm::mat4 GetProjectionMatrix(const float aspect_ratio) const override {
        if (projection_type_ == CameraProjectionType::PERSPECTIVE) {
            return glm::perspective(glm::radians(fov_y_), aspect_ratio, near_plane_, far_plane_);
        }
        const float half_height = far_plane_ * std::tan(glm::radians(fov_y_) * 0.5f);
        return glm::ortho(-half_height * aspect_ratio, half_height * aspect_ratio,
                          -half_height, half_height, near_plane_, far_plane_);
    }

    [[nodiscard]] glm::mat4 GetViewProjectionMatrix(const float aspect_ratio) const override {
        return GetProjectionMatrix(aspect_ratio) * view_matrix_;
    }

    void SetProjectionType(const CameraProjectionType projection_type) override {
        projection_type_ = projection_type;
    }

    [[nodiscard]] CameraProjectionType GetProjectionType() const override {
        return projection_type_;
    }

    void SetMovementMode(const MovementMode movement_mode) override {
        movement_mode_ = movement_mode;
    }

    [[nodiscard]] MovementMode GetMovementMode() const override {
        return movement_mode_;
    }

    void SetConstraints(const CameraConstraints& constraints) override {
        constraints_ = constraints;
        position_ = ClampPositionHeight(position_);
        UpdateViewMatrix();
    }

    [[nodiscard]] CameraConstraints GetConstraints() const override {
        return constraints_;
    }

    bool ProcessInput(const InputEvent& event) override {
        switch (event.type) {
            case InputEvent::Type::MOUSE_BUTTON_PRESS:
            case InputEvent::Type::TOUCH_START:
                dragging_ = true;
                last_pointer_position_ = glm::vec2(event.x, event.y);
                return true;
            case InputEvent::Type::MOUSE_BUTTON_RELEASE:
            case InputEvent::Type::TOUCH_END:
                dragging_ = false;
                return true;
            case InputEvent::Type::MOUSE_MOVE:
            case InputEvent::Type::TOUCH_MOVE: {
                if (!dragging_) {
                    return false;
                }
                // GLFW's cursor-position callback and Qt's mouseMoveEvent
                // both report absolute position, not deltas -- compute the
                // drag delta here rather than requiring every caller to
                // track it (event.dx/dy stay available for genuine
                // relative-input sources, e.g. a locked pointer or a
                // gamepad, which don't report absolute position at all).
                const glm::vec2 current_position(event.x, event.y);
                const glm::vec2 delta = current_position - last_pointer_position_;
                last_pointer_position_ = current_position;
                if (movement_mode_ == MovementMode::FREE) {
                    // FREE is a first-person fly camera: dragging looks
                    // around by rotating the camera's own orientation in
                    // place. Rotate()/Pan() both pivot around `target_`,
                    // which is the correct model for ORBIT but is not a
                    // meaningful pivot in FREE -- WASD there drags `target_`
                    // along rigidly behind the camera, so orbiting around it
                    // does not produce a look-around effect.
                    SetOrientation(heading_degrees_ + static_cast<double>(delta.x) * 0.25,
                                   pitch_degrees_ - static_cast<double>(delta.y) * 0.25,
                                   roll_degrees_);
                } else if (event.button == 2) {
                    Pan(delta.x, delta.y);
                } else {
                    Rotate(delta.x * 0.25f, -delta.y * 0.25f);
                }
                return true;
            }
            case InputEvent::Type::MOUSE_SCROLL:
                Zoom(std::exp(-event.scroll_delta * 0.1f));
                return true;
            case InputEvent::Type::KEY_PRESS:
                return SetMovementKey(event.key, true);
            case InputEvent::Type::KEY_RELEASE:
                return SetMovementKey(event.key, false);
            case InputEvent::Type::DOUBLE_CLICK:
                Zoom(0.5f);
                return true;
        }
        return false;
    }

    [[nodiscard]] AnimationState GetAnimationState() const override {
        if (!animation_.active) {
            return AnimationState::IDLE;
        }
        if (animation_.animate_position) {
            return AnimationState::MOVING;
        }
        return animation_.animate_orientation ? AnimationState::ROTATING : AnimationState::IDLE;
    }

    [[nodiscard]] bool IsAnimating() const override {
        return animation_.active;
    }

    void Reset() override {
        const geodesy::GeodeticPosition camera{
            .latitude_radians = 0.0,
            .longitude_radians = 0.0,
            .ellipsoid_height_meters = kDefaultCameraAltitudeMeters,
        };
        position_ = geodesy::Wgs84Ellipsoid::ToEcef(camera);
        // Globe overview deliberately looks at the ECEF origin. ECEF is a
        // Cartesian frame, so its origin is a valid look target even though
        // it is not a geodetic surface position.
        target_ = geodesy::EcefPosition{glm::dvec3(0.0)};
        fov_y_ = 45.0f;
        near_plane_ = 1000.0f;
        far_plane_ = 30000000.0f;
        clipping_planes_explicit_ = false;
        projection_type_ = CameraProjectionType::PERSPECTIVE;
        movement_mode_ = MovementMode::ORBIT;
        movement_forward_ = movement_right_ = movement_up_ = 0.0f;
        dragging_ = false;
        animation_.Reset();
        UpdateOrientationFromTarget();
        UpdateClippingPlanes();
        UpdateViewMatrix();
    }

    void AnimateToGeographic(
        const double longitude,
        const double latitude,
        const double altitude,
        const float duration) override {
        StartGeographicAnimation(longitude, latitude, altitude, duration, false);
    }

    void AnimateToOrientation(
        const double heading,
        const double pitch,
        const double roll,
        const float duration) override {
        animation_.Reset();
        animation_.active = true;
        animation_.duration_seconds = std::max(duration, 0.0f);
        animation_.start_orientation = {heading_degrees_, pitch_degrees_, roll_degrees_};
        animation_.target_orientation = {
            NormalizeDegrees(heading),
            std::clamp(pitch, static_cast<double>(constraints_.min_pitch),
                       static_cast<double>(constraints_.max_pitch)),
            std::clamp(roll, -180.0, 180.0),
        };
        animation_.animate_orientation = true;
    }

    void StopAnimations() override {
        animation_.Reset();
    }

    void Zoom(const float factor) override {
        if (!(factor > 0.0f)) {
            return;
        }
        const glm::dvec3 offset = position_.meters - target_.meters;
        const double distance = glm::length(offset);
        if (distance <= kMinimumLookDistanceMeters) {
            return;
        }
        position_.meters = target_.meters + offset * static_cast<double>(factor);
        position_ = ClampPositionHeight(position_);
        UpdateOrientationFromTarget();
        UpdateClippingPlanes();
        UpdateViewMatrix();
    }

    void Pan(const float screen_dx, const float screen_dy) override {
        const auto target_geodetic = geodesy::Wgs84Ellipsoid::FromEcef(target_);
        const auto camera_geodetic = geodesy::Wgs84Ellipsoid::FromEcef(position_);
        if (!camera_geodetic.has_value()) {
            return;
        }
        if (!target_geodetic.has_value()) {
            // Globe overview orbits the Cartesian Earth centre. A centre has
            // no geodetic latitude/longitude, so route a drag through the
            // physical orbit operation instead of inventing a fake surface
            // coordinate for it.
            Rotate(-screen_dx * 0.15f, -screen_dy * 0.15f);
            return;
        }

        const double camera_height = std::max(camera_geodetic->ellipsoid_height_meters,
                                              static_cast<double>(constraints_.min_altitude));
        const double metres_per_input_unit = std::max(camera_height * 0.002, 1.0);
        const double east_meters = -static_cast<double>(screen_dx) * metres_per_input_unit;
        const double north_meters = static_cast<double>(screen_dy) * metres_per_input_unit;
        target_ = geodesy::Wgs84Ellipsoid::ToEcef(OffsetGeodetic(*target_geodetic, east_meters, north_meters));
        position_ = ClampPositionHeight(geodesy::Wgs84Ellipsoid::ToEcef(
            OffsetGeodetic(*camera_geodetic, east_meters, north_meters)));
        UpdateOrientationFromTarget();
        UpdateViewMatrix();
    }

    void Rotate(const float delta_heading, const float delta_pitch) override {
        const auto target_geodetic = geodesy::Wgs84Ellipsoid::FromEcef(target_);
        if (!target_geodetic.has_value()) {
            const glm::dquat yaw = glm::angleAxis(
                DegreesToRadians(-delta_heading), glm::dvec3(0.0, 0.0, 1.0));
            const glm::dquat pitch = glm::angleAxis(
                DegreesToRadians(delta_pitch), glm::dvec3(0.0, 1.0, 0.0));
            position_.meters = pitch * yaw * position_.meters;
            UpdateOrientationFromTarget();
            UpdateViewMatrix();
            return;
        }
        const renderer::EcefRenderFrame frame = renderer::EcefRenderFrame::FromCamera(*target_geodetic);
        glm::dvec3 local_camera = frame.ToLocal(position_);
        const glm::dquat yaw = glm::angleAxis(DegreesToRadians(-delta_heading), glm::dvec3(0.0, 0.0, 1.0));
        local_camera = yaw * local_camera;
        const glm::dvec3 local_forward = NormalizedOr(-local_camera, glm::dvec3(0.0, 0.0, -1.0));
        // See GetRightVector(): must be the fixed ECEF polar axis in this
        // frame's basis, not this frame's own local Up.
        const glm::dvec3 world_north_axis = frame.ToLocalDirection(glm::dvec3(0.0, 0.0, 1.0));
        const glm::dvec3 local_right = NormalizedOr(glm::cross(local_forward, world_north_axis),
                                                    glm::dvec3(1.0, 0.0, 0.0));
        const glm::dquat pitch = glm::angleAxis(DegreesToRadians(delta_pitch), local_right);
        position_ = ClampPositionHeight(frame.FromLocal(pitch * local_camera));
        UpdateOrientationFromTarget();
        UpdateViewMatrix();
    }

    void FlyTo(const double longitude, const double latitude, const double altitude_meters,
               const float duration_seconds) override {
        StartGeographicAnimation(longitude, latitude, altitude_meters, duration_seconds, true);
    }

    void LookAt(const geodesy::EcefPosition& target) override {
        SetEcefTarget(target);
    }

    [[nodiscard]] glm::vec3 GetForwardVector() const override {
        const renderer::EcefRenderFrame frame = CurrentRenderFrame();
        return glm::vec3(NormalizedOr(frame.ToLocal(target_), glm::dvec3(0.0, 0.0, -1.0)));
    }

    [[nodiscard]] glm::vec3 GetRightVector() const override {
        const renderer::EcefRenderFrame frame = CurrentRenderFrame();
        // (0,0,1) here must be the fixed ECEF polar axis expressed in this
        // frame's local basis, not the frame's own local Up (which is the
        // camera's radial "up" and is nearly antiparallel to forward for
        // any orbit-at-centre camera at every latitude, not just the
        // poles -- using it directly made the degenerate branch below
        // trigger almost everywhere, with the tie-break sign flipping
        // discontinuously across the equator).
        const glm::dvec3 world_north_axis = frame.ToLocalDirection(glm::dvec3(0.0, 0.0, 1.0));
        return glm::vec3(NormalizedOr(glm::cross(glm::dvec3(GetForwardVector()), world_north_axis),
                                      glm::dvec3(1.0, 0.0, 0.0)));
    }

    [[nodiscard]] glm::vec3 GetUpVector() const override {
        return glm::normalize(glm::cross(GetRightVector(), GetForwardVector()));
    }

    [[nodiscard]] std::pair<geodesy::EcefPosition, glm::dvec3> ScreenToEcefRay(
        const float screen_x,
        const float screen_y,
        const float aspect_ratio) const override {
        const glm::mat4 inverse = glm::inverse(GetViewProjectionMatrix(aspect_ratio));
        const float x = screen_x * 2.0f - 1.0f;
        const float y = 1.0f - screen_y * 2.0f;
        glm::vec4 near_point = inverse * glm::vec4(x, y, -1.0f, 1.0f);
        glm::vec4 far_point = inverse * glm::vec4(x, y, 1.0f, 1.0f);
        near_point /= near_point.w;
        far_point /= far_point.w;
        const renderer::EcefRenderFrame frame = CurrentRenderFrame();
        const geodesy::EcefPosition origin = frame.FromLocal(glm::dvec3(near_point));
        const geodesy::EcefPosition end = frame.FromLocal(glm::dvec3(far_point));
        return {origin, NormalizedOr(end.meters - origin.meters, glm::dvec3(0.0, 0.0, -1.0))};
    }

    [[nodiscard]] std::optional<glm::vec2> EcefToScreen(
        const geodesy::EcefPosition& position,
        const float aspect_ratio) const override {
        const renderer::EcefRenderFrame frame = CurrentRenderFrame();
        const glm::vec3 local_position(frame.ToLocal(position));
        const glm::vec4 clip =
            GetViewProjectionMatrix(aspect_ratio) * glm::vec4(local_position, 1.0f);
        if (clip.w <= 0.0f) {
            return std::nullopt;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    }

private:
    [[nodiscard]] double ClampHeight(const double height) const noexcept {
        return std::clamp(height,
                          static_cast<double>(constraints_.min_altitude),
                          static_cast<double>(constraints_.max_altitude));
    }

    [[nodiscard]] geodesy::EcefPosition ClampPositionHeight(
        const geodesy::EcefPosition& position) const noexcept {
        const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(position);
        if (!geodetic.has_value()) {
            return position_;
        }
        geodesy::GeodeticPosition clamped = *geodetic;
        clamped.ellipsoid_height_meters = ClampHeight(clamped.ellipsoid_height_meters);
        return geodesy::Wgs84Ellipsoid::ToEcef(clamped);
    }

    [[nodiscard]] renderer::EcefRenderFrame CurrentRenderFrame() const {
        const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(position_);
        return renderer::EcefRenderFrame::FromCamera(geodetic.value_or(
            geodesy::GeodeticPosition{0.0, 0.0, kDefaultCameraAltitudeMeters}));
    }

    [[nodiscard]] geodesy::GeodeticPosition OffsetGeodetic(
        const geodesy::GeodeticPosition& source,
        const double east_meters,
        const double north_meters) const noexcept {
        geodesy::GeodeticPosition result = source;
        const double radius = geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters + source.ellipsoid_height_meters;
        result.latitude_radians = std::clamp(source.latitude_radians + north_meters / radius,
                                             -glm::half_pi<double>(), glm::half_pi<double>());
        const double longitude_radius = std::max(radius * std::abs(std::cos(source.latitude_radians)), 1.0);
        result.longitude_radians += east_meters / longitude_radius;
        return result;
    }

    void EnsureValidTarget() noexcept {
        if (glm::length(target_.meters - position_.meters) > kMinimumLookDistanceMeters) {
            return;
        }
        const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(position_);
        if (!geodetic.has_value()) {
            return;
        }
        geodesy::GeodeticPosition surface = *geodetic;
        surface.ellipsoid_height_meters = 0.0;
        target_ = geodesy::Wgs84Ellipsoid::ToEcef(surface);
    }

    void UpdateOrientationFromTarget() noexcept {
        const renderer::EcefRenderFrame frame = CurrentRenderFrame();
        const glm::dvec3 direction = NormalizedOr(frame.ToLocal(target_), glm::dvec3(0.0, 0.0, -1.0));
        heading_degrees_ = NormalizeDegrees(RadiansToDegrees(std::atan2(direction.x, direction.y)));
        pitch_degrees_ = RadiansToDegrees(std::asin(std::clamp(direction.z, -1.0, 1.0)));
    }

    void UpdateClippingPlanes() noexcept {
        if (clipping_planes_explicit_) {
            return;
        }
        const double look_distance = glm::length(position_.meters - target_.meters);
        near_plane_ = static_cast<float>(std::max(0.1, look_distance * 0.001));
        far_plane_ = static_cast<float>(std::max(30000000.0, look_distance * 4.0));
    }

    void UpdateViewMatrix() noexcept {
        const renderer::EcefRenderFrame frame = CurrentRenderFrame();
        glm::dvec3 local_target = frame.ToLocal(target_);
        if (glm::length(local_target) < kMinimumLookDistanceMeters) {
            local_target = glm::dvec3(0.0, 0.0, -kMinimumLookDistanceMeters);
        }
        // The lookAt "up" hint must be a globally-fixed reference (the ECEF
        // polar axis, expressed in this frame's basis) so it stays
        // continuous as the camera moves. Using this frame's own local Up
        // (radial "up" at the camera) instead would make it nearly
        // antiparallel to `local_target` for any orbit-at-centre camera at
        // *every* latitude, not just near the poles, since the target is
        // always roughly straight down -- the degenerate branch below would
        // fire almost everywhere, and the tie-break sign would flip
        // discontinuously exactly at the equator (the one place the
        // near-zero residual crosses sign), producing the reported
        // upside-down flip on equator crossing.
        glm::dvec3 local_up = frame.ToLocalDirection(glm::dvec3(0.0, 0.0, 1.0));
        if (glm::length(glm::cross(NormalizedOr(local_target, -local_up), local_up)) < 1e-8) {
            local_up = glm::dvec3(0.0, 1.0, 0.0);
        }
        view_matrix_ = glm::lookAt(glm::vec3(0.0f), glm::vec3(local_target), glm::vec3(local_up));
    }

    void StartGeographicAnimation(
        const double longitude,
        const double latitude,
        const double altitude,
        const float duration,
        const bool target_surface) {
        const auto current_camera = geodesy::Wgs84Ellipsoid::FromEcef(position_);
        const auto current_target = geodesy::Wgs84Ellipsoid::FromEcef(target_);
        if (!current_camera.has_value() || !current_target.has_value()) {
            return;
        }
        animation_.Reset();
        animation_.active = true;
        animation_.duration_seconds = std::max(duration, 0.0f);
        animation_.start_camera = *current_camera;
        animation_.start_target = *current_target;
        animation_.target_camera = GeographicDegrees(longitude, latitude, ClampHeight(altitude));
        animation_.target_target = animation_.target_camera;
        animation_.target_target.ellipsoid_height_meters = target_surface ? 0.0 : current_target->ellipsoid_height_meters;
        animation_.animate_position = true;
        if (animation_.duration_seconds == 0.0f) {
            UpdateAnimation(0.0f);
        }
    }

    [[nodiscard]] static geodesy::GeodeticPosition InterpolateGeodetic(
        const geodesy::GeodeticPosition& start,
        const geodesy::GeodeticPosition& end,
        const double t) noexcept {
        return {
            .latitude_radians = glm::mix(start.latitude_radians, end.latitude_radians, t),
            .longitude_radians = start.longitude_radians +
                ShortestLongitudeDelta(start.longitude_radians, end.longitude_radians) * t,
            .ellipsoid_height_meters = glm::mix(start.ellipsoid_height_meters,
                                                 end.ellipsoid_height_meters, t),
        };
    }

    void UpdateAnimation(const float delta_time) {
        if (!animation_.active) {
            return;
        }
        animation_.elapsed_seconds += delta_time;
        const float linear = animation_.duration_seconds <= 0.0f
            ? 1.0f
            : std::clamp(animation_.elapsed_seconds / animation_.duration_seconds, 0.0f, 1.0f);
        const double eased = EaseInOutCubic(linear);
        if (animation_.animate_position) {
            position_ = geodesy::Wgs84Ellipsoid::ToEcef(
                InterpolateGeodetic(animation_.start_camera, animation_.target_camera, eased));
            target_ = geodesy::Wgs84Ellipsoid::ToEcef(
                InterpolateGeodetic(animation_.start_target, animation_.target_target, eased));
            UpdateOrientationFromTarget();
        }
        if (animation_.animate_orientation) {
            const glm::dvec3 orientation = glm::mix(animation_.start_orientation,
                                                     animation_.target_orientation, eased);
            SetOrientation(orientation.x, orientation.y, orientation.z);
        }
        if (linear >= 1.0f) {
            animation_.Reset();
        }
    }

    void UpdateMovement(const float delta_time) {
        // WASD is a FREE-mode fly-camera control, not an orbit control --
        // ORBIT's mouse drag already owns translation there (Pan/Rotate).
        // This also sidesteps the landmine below: ORBIT's target sits at
        // the exact ECEF origin (Earth's centre), which has no geodetic
        // latitude/longitude by definition, so any movement implementation
        // that needs `target_`'s geodetic form -- as the previous version
        // did -- silently no-ops for the entire default configuration.
        if (movement_mode_ != MovementMode::FREE) {
            return;
        }
        if (movement_forward_ == 0.0f && movement_right_ == 0.0f && movement_up_ == 0.0f) {
            return;
        }
        const auto camera_geodetic = geodesy::Wgs84Ellipsoid::FromEcef(position_);
        if (!camera_geodetic.has_value()) {
            return;
        }
        const double speed = std::max(static_cast<double>(constraints_.max_movement_speed),
                                      camera_geodetic->ellipsoid_height_meters * 0.25);
        const double metres = speed * delta_time;

        // Translate directly along the camera's own local frame -- forward
        // (W/S), right (A/D) and local radial up (Q/E, ascend/descend) --
        // instead of routing through `target_`'s geodetic form. This works
        // regardless of where `target_` happens to be.
        const renderer::EcefRenderFrame frame = CurrentRenderFrame();
        const glm::dvec3 local_delta =
            glm::dvec3(GetForwardVector()) * static_cast<double>(movement_forward_) * metres +
            glm::dvec3(GetRightVector()) * static_cast<double>(movement_right_) * metres +
            glm::dvec3(0.0, 0.0, 1.0) * static_cast<double>(movement_up_) * metres;

        const geodesy::EcefPosition old_position = position_;
        position_ = ClampPositionHeight(frame.FromLocal(local_delta));
        // Carry the target along by the same (possibly clamped) delta so a
        // pure fly-through translation doesn't change where the camera is
        // looking -- heading/pitch fall out of this unchanged.
        target_.meters += position_.meters - old_position.meters;
        UpdateOrientationFromTarget();
    }

    bool SetMovementKey(const int key, const bool pressed) noexcept {
        const float value = pressed ? 1.0f : 0.0f;
        switch (key) {
            case 'W': case 'w': movement_forward_ = value; return true;
            case 'S': case 's': movement_forward_ = -value; return true;
            case 'A': case 'a': movement_right_ = -value; return true;
            case 'D': case 'd': movement_right_ = value; return true;
            case 'Q': case 'q': movement_up_ = -value; return true;
            case 'E': case 'e': movement_up_ = value; return true;
            default: return false;
        }
    }

    Configuration config_;
    bool initialized_ = false;
    geodesy::EcefPosition position_{};
    geodesy::EcefPosition target_{};
    double heading_degrees_ = 0.0;
    double pitch_degrees_ = -90.0;
    double roll_degrees_ = 0.0;
    float fov_y_ = 45.0f;
    float near_plane_ = 1000.0f;
    float far_plane_ = 30000000.0f;
    bool clipping_planes_explicit_ = false;
    glm::mat4 view_matrix_{1.0f};
    CameraProjectionType projection_type_ = CameraProjectionType::PERSPECTIVE;
    MovementMode movement_mode_ = MovementMode::ORBIT;
    CameraConstraints constraints_{};
    CameraAnimation animation_{};
    bool dragging_ = false;
    // Anchors the current drag gesture. GLFW's cursor-position callback and
    // Qt's mouseMoveEvent both report absolute position, not deltas -- every
    // real caller needs this tracked somewhere, so it belongs here once
    // rather than duplicated (and, historically, forgotten) in each embedder.
    glm::vec2 last_pointer_position_{0.0f};
    float movement_forward_ = 0.0f;
    float movement_right_ = 0.0f;
    float movement_up_ = 0.0f;
};

class PerspectiveCamera final : public CameraImpl {
public:
    explicit PerspectiveCamera(const Configuration& config)
        : CameraImpl(config) {
        SetProjectionType(CameraProjectionType::PERSPECTIVE);
    }
};

class OrthographicCamera final : public CameraImpl {
public:
    explicit OrthographicCamera(const Configuration& config)
        : CameraImpl(config) {
        SetProjectionType(CameraProjectionType::ORTHOGRAPHIC);
    }
};

}  // namespace

std::unique_ptr<Camera> CreatePerspectiveCamera(const Configuration& config) {
    return std::make_unique<PerspectiveCamera>(config);
}

std::unique_ptr<Camera> CreateOrthographicCamera(const Configuration& config) {
    return std::make_unique<OrthographicCamera>(config);
}

}  // namespace earth_map
