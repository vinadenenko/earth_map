/**
 * @file map_interaction.cpp
 * @brief Implementation of high-level map interaction API
 */

#include "../../include/earth_map/api/map_interaction.h"
#include "../../include/earth_map/constants.h"
#include "../../include/earth_map/geodesy/wgs84_ellipsoid.h"
#include "../../include/earth_map/renderer/renderer.h"
#include "../../include/earth_map/renderer/camera.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <spdlog/spdlog.h>

namespace earth_map {
namespace api {

using namespace coordinates;

namespace {

std::optional<geodesy::GeodeticPosition> ToGeodetic(const Geographic& location) {
    const geodesy::GeodeticPosition geodetic{
        constants::conversion::DegreesToRadians(location.latitude),
        constants::conversion::DegreesToRadians(location.longitude),
        location.altitude};
    return geodetic.IsValid() ? std::optional<geodesy::GeodeticPosition>(geodetic) : std::nullopt;
}

Geographic ToGeographic(const geodesy::GeodeticPosition& geodetic) {
    return Geographic{
        constants::conversion::RadiansToDegrees(geodetic.latitude_radians),
        constants::conversion::RadiansToDegrees(geodetic.longitude_radians),
        geodetic.ellipsoid_height_meters};
}

}  // namespace

// ============================================================================
// Pimpl Implementation
// ============================================================================

class MapInteraction::Impl {
public:
    explicit Impl(std::shared_ptr<Renderer> renderer)
        : renderer_(std::move(renderer)) {
        if (!renderer_) {
            throw std::invalid_argument("MapInteraction requires valid Renderer");
        }
    }

    std::shared_ptr<Renderer> renderer_;
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

MapInteraction::MapInteraction(std::shared_ptr<Renderer> renderer)
    : impl_(std::make_unique<Impl>(std::move(renderer))) {
    spdlog::debug("MapInteraction created");
}

MapInteraction::~MapInteraction() = default;

MapInteraction::MapInteraction(MapInteraction&&) noexcept = default;
MapInteraction& MapInteraction::operator=(MapInteraction&&) noexcept = default;

// ============================================================================
// Screen ↔ Geographic Conversions
// ============================================================================

std::optional<Geographic> MapInteraction::GetLocationAtScreenPoint(
    int screen_x, int screen_y) const {

    if (!impl_->renderer_) {
        return std::nullopt;
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return std::nullopt;
    }

    auto [width, height] = GetViewportSize();
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    const float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    const float normalized_x = static_cast<float>(screen_x) / static_cast<float>(width);
    const float normalized_y = static_cast<float>(screen_y) / static_cast<float>(height);

    const auto [origin, direction] =
        camera_controller->ScreenToEcefRay(normalized_x, normalized_y, aspect_ratio);
    const auto intersection = geodesy::Wgs84Ellipsoid::IntersectRay(origin, direction);
    if (!intersection.has_value()) {
        return std::nullopt;
    }
    const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(*intersection);
    if (!geodetic.has_value()) {
        return std::nullopt;
    }
    return ToGeographic(*geodetic);
}

std::optional<Screen> MapInteraction::GetScreenPointForLocation(
    const Geographic& location) const {

    if (!impl_->renderer_) {
        return std::nullopt;
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return std::nullopt;
    }

    const auto geodetic = ToGeodetic(location);
    if (!geodetic.has_value()) {
        return std::nullopt;
    }

    auto [width, height] = GetViewportSize();
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    const float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);

    const auto normalized = camera_controller->EcefToScreen(
        geodesy::Wgs84Ellipsoid::ToEcef(*geodetic), aspect_ratio);
    if (!normalized.has_value()) {
        return std::nullopt;
    }

    return Screen{static_cast<double>(normalized->x) * width,
                 static_cast<double>(normalized->y) * height};
}

// ============================================================================
// Visibility Queries
// ============================================================================

GeographicBounds MapInteraction::GetVisibleBounds() const {
    if (!impl_->renderer_) {
        return GeographicBounds();
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return GeographicBounds();
    }

    auto [width, height] = GetViewportSize();
    if (width <= 0 || height <= 0) {
        return GeographicBounds();
    }
    const float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);

    // Sample screen-space corners/edges/centre and intersect each ray with
    // the WGS84 ellipsoid, matching the ECEF/ENU render contract instead of
    // the legacy normalized-sphere CoordinateMapper path.
    constexpr std::array<glm::vec2, 9> kSamplePoints = {{
        {0.5f, 0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},
        {0.5f, 0.0f}, {0.5f, 1.0f}, {0.0f, 0.5f}, {1.0f, 0.5f},
    }};

    double min_latitude = std::numeric_limits<double>::infinity();
    double max_latitude = -std::numeric_limits<double>::infinity();
    double min_longitude = std::numeric_limits<double>::infinity();
    double max_longitude = -std::numeric_limits<double>::infinity();

    for (const glm::vec2& sample : kSamplePoints) {
        const auto [origin, direction] =
            camera_controller->ScreenToEcefRay(sample.x, sample.y, aspect_ratio);
        const auto intersection = geodesy::Wgs84Ellipsoid::IntersectRay(origin, direction);
        if (!intersection.has_value()) {
            continue;
        }
        const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(*intersection);
        if (!geodetic.has_value()) {
            continue;
        }
        const Geographic point = ToGeographic(*geodetic);
        min_latitude = std::min(min_latitude, point.latitude);
        max_latitude = std::max(max_latitude, point.latitude);
        min_longitude = std::min(min_longitude, point.longitude);
        max_longitude = std::max(max_longitude, point.longitude);
    }

    if (!std::isfinite(min_latitude) || !std::isfinite(max_latitude) ||
        !std::isfinite(min_longitude) || !std::isfinite(max_longitude)) {
        // No sample hit the globe (camera looking entirely at space).
        return GeographicBounds();
    }

    return GeographicBounds({min_latitude, min_longitude}, {max_latitude, max_longitude});
}

bool MapInteraction::IsLocationVisible(const Geographic& location) const {
    // Check if location is within visible bounds
    GeographicBounds bounds = GetVisibleBounds();
    if (!bounds.IsValid()) {
        return false;
    }

    // Check if location is in bounds
    if (!bounds.Contains(location)) {
        return false;
    }

    // Verify it actually projects to screen (not behind camera)
    auto screen = GetScreenPointForLocation(location);
    return screen.has_value();
}

// ============================================================================
// Distance and Bearing Calculations
// ============================================================================

double MapInteraction::MeasureDistance(
    const Geographic& from,
    const Geographic& to) const noexcept {

    return CalculateGreatCircleDistance(from, to);
}

double MapInteraction::CalculateBearing(
    const Geographic& from,
    const Geographic& to) const noexcept {

    return coordinates::CalculateBearing(from, to);
}

// ============================================================================
// Camera Information
// ============================================================================

Geographic MapInteraction::GetCameraLocation() const {
    if (!impl_->renderer_) {
        return Geographic();
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return Geographic();
    }

    const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(camera_controller->GetEcefPosition());
    return geodetic.has_value() ? ToGeographic(*geodetic) : Geographic();
}

Geographic MapInteraction::GetCameraTarget() const {
    if (!impl_->renderer_) {
        return Geographic();
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return Geographic();
    }

    const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(camera_controller->GetEcefTarget());
    return geodetic.has_value() ? ToGeographic(*geodetic) : Geographic();
}

double MapInteraction::GetCameraAltitude() const {
    if (!impl_->renderer_) {
        return 0.0;
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return 0.0;
    }

    const auto geodetic = geodesy::Wgs84Ellipsoid::FromEcef(camera_controller->GetEcefPosition());
    return geodetic.has_value() ? geodetic->ellipsoid_height_meters : 0.0;
}

// ============================================================================
// Camera Control
// ============================================================================

void MapInteraction::FlyToLocation(
    const Geographic& location,
    double altitude,
    double duration) {

    if (!impl_->renderer_) {
        return;
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return;
    }

    // CameraController::FlyTo already animates in real WGS84 geodetic units;
    // no normalized-sphere conversion is needed here.
    camera_controller->FlyTo(location.longitude, location.latitude, altitude,
                             static_cast<float>(duration));

    spdlog::info("Flying to location: lat={:.4f}, lon={:.4f}, altitude={:.0f}m",
                 location.latitude, location.longitude, altitude);
}

void MapInteraction::SetCameraView(
    const Geographic& location,
    double altitude) {

    if (!impl_->renderer_) {
        return;
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return;
    }

    camera_controller->SetGeographicPosition(location.longitude, location.latitude, altitude);

    spdlog::debug("Camera view set to: lat={:.4f}, lon={:.4f}, altitude={:.0f}m",
                  location.latitude, location.longitude, altitude);
}

// ============================================================================
// Viewport Information
// ============================================================================

std::pair<int, int> MapInteraction::GetViewportSize() const {
    if (!impl_->renderer_) {
        return {1024, 768};  // Default fallback
    }

    // Get viewport from renderer
    // For now, return default - in real implementation would query from renderer
    return {1024, 768};
}

} // namespace api
} // namespace earth_map
