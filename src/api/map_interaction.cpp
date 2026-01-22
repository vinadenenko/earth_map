/**
 * @file map_interaction.cpp
 * @brief Implementation of high-level map interaction API
 */

#include "../../include/earth_map/api/map_interaction.h"
#include "../../include/earth_map/coordinates/coordinate_mapper.h"
#include "../../include/earth_map/renderer/renderer.h"
#include "../../include/earth_map/renderer/camera.h"
#include <spdlog/spdlog.h>

namespace earth_map {
namespace api {

using namespace coordinates;

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

    // Get camera controller from renderer
    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return std::nullopt;
    }

    // Get matrices from camera controller
    glm::mat4 view_matrix = camera_controller->GetViewMatrix();
    auto [width, height] = GetViewportSize();
    float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    glm::mat4 proj_matrix = camera_controller->GetProjectionMatrix(aspect_ratio);

    // Get viewport dimensions
    glm::ivec4 viewport(0, 0, width, height);

    // Convert screen coordinates
    Screen screen(static_cast<double>(screen_x), static_cast<double>(screen_y));

    // Use CoordinateMapper for conversion
    return CoordinateMapper::ScreenToGeographic(
        screen, view_matrix, proj_matrix, viewport, 1.0f  // Globe radius = 1.0
    );
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

    glm::mat4 view_matrix = camera_controller->GetViewMatrix();
    auto [width, height] = GetViewportSize();
    float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    glm::mat4 proj_matrix = camera_controller->GetProjectionMatrix(aspect_ratio);

    glm::ivec4 viewport(0, 0, width, height);

    return CoordinateMapper::GeographicToScreen(
        location, view_matrix, proj_matrix, viewport
    );
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

    // Get camera position in world space
    glm::vec3 camera_pos = camera_controller->GetPosition();
    World camera_world(camera_pos);

    glm::mat4 view_matrix = camera_controller->GetViewMatrix();
    auto [width, height] = GetViewportSize();
    float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    glm::mat4 proj_matrix = camera_controller->GetProjectionMatrix(aspect_ratio);

    return CoordinateMapper::CalculateVisibleGeographicBounds(
        camera_world, view_matrix, proj_matrix, 1.0f
    );
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

    // Convert camera position to geographic
    glm::vec3 camera_pos = camera_controller->GetPosition();
    World camera_world(camera_pos);

    return CoordinateMapper::WorldToGeographic(camera_world, 1.0f);
}

Geographic MapInteraction::GetCameraTarget() const {
    if (!impl_->renderer_) {
        return Geographic();
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return Geographic();
    }

    // For orbital camera: target is where camera is looking (toward origin)
    glm::vec3 camera_pos = camera_controller->GetPosition();
    glm::vec3 look_direction = -glm::normalize(camera_pos);

    // Convert look direction to geographic
    World target_world(look_direction);
    return CoordinateMapper::WorldToGeographic(target_world, 1.0f);
}

double MapInteraction::GetCameraAltitude() const {
    if (!impl_->renderer_) {
        return 0.0;
    }

    auto camera_controller = impl_->renderer_->GetCameraController();
    if (!camera_controller) {
        return 0.0;
    }

    // Distance from camera to globe surface
    glm::vec3 camera_pos = camera_controller->GetPosition();
    float distance = glm::length(camera_pos);

    // Altitude = distance - globe_radius
    // Globe radius = 1.0 in normalized units
    // Convert to meters (approximate)
    constexpr double EARTH_RADIUS_METERS = 6371000.0;
    return static_cast<double>(distance - 1.0f) * EARTH_RADIUS_METERS;
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

    // Convert geographic location to world position
    World target_world = CoordinateMapper::GeographicToWorld(location, 1.0f);

    // Calculate camera position at specified altitude
    // Camera should be at target_direction * (1.0 + normalized_altitude)
    glm::vec3 direction = target_world.Direction();

    // Normalize altitude to globe radius units
    constexpr double EARTH_RADIUS_METERS = 6371000.0;
    float normalized_altitude = static_cast<float>(altitude / EARTH_RADIUS_METERS);

    glm::vec3 new_camera_pos = direction * (1.0f + normalized_altitude);

    // Set camera to new position (animation not yet implemented in CameraController)
    // TODO: Implement smooth animation when CameraController supports it
    (void)duration;  // Suppress unused parameter warning
    camera_controller->SetPosition(new_camera_pos);

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

    // Convert geographic location to world position
    World target_world = CoordinateMapper::GeographicToWorld(location, 1.0f);
    glm::vec3 direction = target_world.Direction();

    // Calculate camera position
    constexpr double EARTH_RADIUS_METERS = 6371000.0;
    float normalized_altitude = static_cast<float>(altitude / EARTH_RADIUS_METERS);
    glm::vec3 new_camera_pos = direction * (1.0f + normalized_altitude);

    // Set camera position immediately
    camera_controller->SetPosition(new_camera_pos);

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
