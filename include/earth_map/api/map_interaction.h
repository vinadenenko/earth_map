/**
 * @file map_interaction.h
 * @brief High-level user-facing API for map interaction
 *
 * This interface provides simple, intuitive methods for interacting with the map.
 * Users work exclusively with geographic coordinates and screen pixels - all
 * internal coordinate system complexity is hidden.
 *
 * Design Principles:
 * - User-friendly: Simple method names, clear semantics
 * - No OpenGL exposure: Users never see matrices, world space, etc.
 * - Type-safe: Uses strong coordinate types from coordinate_spaces.h
 * - Self-documenting: Method names clearly express intent
 *
 * @see COORDINATE_SYSTEM_ARCHITECTURE_PLAN.md for architecture details
 */

#pragma once

#include <earth_map/coordinates/coordinate_spaces.h>
#include <glm/glm.hpp>
#include <memory>
#include <optional>

namespace earth_map {

// Forward declarations
class Renderer;
class Camera;

namespace api {

/**
 * @brief High-level interface for map interaction
 *
 * This is the primary API for library users. It provides intuitive methods for:
 * - Converting between screen and geographic coordinates
 * - Querying visible area
 * - Measuring distances
 * - Camera control
 *
 * Example usage:
 * @code
 * auto map = earth_map::CreateMap(...);
 * auto interaction = map->GetInteraction();
 *
 * // User clicks at screen position (500, 300)
 * auto location = interaction->GetLocationAtScreenPoint(500, 300);
 * if (location) {
 *     std::cout << "Clicked: " << location->latitude << ", " << location->longitude;
 * }
 *
 * // Draw marker at New York City
 * coordinates::Geographic nyc(40.7128, -74.0060, 0.0);
 * auto screen_pos = interaction->GetScreenPointForLocation(nyc);
 * if (screen_pos) {
 *     DrawMarker(screen_pos->x, screen_pos->y);
 * }
 * @endcode
 */
class MapInteraction {
public:
    /**
     * @brief Constructor
     * @param renderer Renderer instance (used to access camera and matrices)
     */
    explicit MapInteraction(std::shared_ptr<Renderer> renderer);

    /**
     * @brief Destructor
     */
    ~MapInteraction();

    // Prevent copying
    MapInteraction(const MapInteraction&) = delete;
    MapInteraction& operator=(const MapInteraction&) = delete;

    // Allow moving
    MapInteraction(MapInteraction&&) noexcept;
    MapInteraction& operator=(MapInteraction&&) noexcept;

    // ========================================================================
    // Screen ↔ Geographic Conversions (Primary User API)
    // ========================================================================

    /**
     * @brief Convert screen click to geographic coordinates
     *
     * Typical use case: User clicks on map, application needs to know
     * which geographic location was clicked.
     *
     * @param screen_x Pixel X coordinate from left edge
     * @param screen_y Pixel Y coordinate from top edge
     * @return Geographic coordinates, or nullopt if click didn't hit globe
     *
     * @example
     * @code
     * // User clicked at (500, 300)
     * auto location = interaction->GetLocationAtScreenPoint(500, 300);
     * if (location) {
     *     std::cout << "Lat: " << location->latitude
     *               << ", Lon: " << location->longitude << std::endl;
     * }
     * @endcode
     */
    [[nodiscard]] std::optional<coordinates::Geographic> GetLocationAtScreenPoint(
        int screen_x, int screen_y) const;

    /**
     * @brief Convert geographic location to screen coordinates
     *
     * Typical use case: Application wants to draw a marker at a specific
     * geographic location (e.g., city, user position, POI).
     *
     * @param location Geographic coordinates
     * @return Screen coordinates in pixels, or nullopt if location not visible
     *
     * @example
     * @code
     * coordinates::Geographic nyc(40.7128, -74.0060, 0.0);
     * auto screen = interaction->GetScreenPointForLocation(nyc);
     * if (screen) {
     *     DrawMarker(screen->x, screen->y);
     * }
     * @endcode
     */
    [[nodiscard]] std::optional<coordinates::Screen> GetScreenPointForLocation(
        const coordinates::Geographic& location) const;

    // ========================================================================
    // Visibility Queries
    // ========================================================================

    /**
     * @brief Get geographic bounds of current view
     *
     * Returns the region of Earth currently visible in the viewport.
     * Useful for determining which POIs/labels to display.
     *
     * @return Bounding box of visible area in degrees
     *
     * @example
     * @code
     * auto bounds = interaction->GetVisibleBounds();
     * // Query database for POIs within bounds
     * auto pois = database.GetPOIs(bounds.min.latitude, bounds.min.longitude,
     *                               bounds.max.latitude, bounds.max.longitude);
     * @endcode
     */
    [[nodiscard]] coordinates::GeographicBounds GetVisibleBounds() const;

    /**
     * @brief Check if a location is currently visible
     *
     * @param location Geographic coordinates to test
     * @return true if location is in current view
     *
     * @example
     * @code
     * coordinates::Geographic paris(48.8566, 2.3522, 0.0);
     * if (interaction->IsLocationVisible(paris)) {
     *     // Draw Paris label
     * }
     * @endcode
     */
    [[nodiscard]] bool IsLocationVisible(const coordinates::Geographic& location) const;

    // ========================================================================
    // Distance and Bearing Calculations
    // ========================================================================

    /**
     * @brief Measure great circle distance between two locations
     *
     * Uses Haversine formula to calculate shortest distance over Earth's surface.
     *
     * @param from Start location
     * @param to End location
     * @return Distance in meters
     *
     * @example
     * @code
     * coordinates::Geographic nyc(40.7128, -74.0060, 0.0);
     * coordinates::Geographic london(51.5074, -0.1278, 0.0);
     * double distance = interaction->MeasureDistance(nyc, london);
     * std::cout << "NYC to London: " << distance / 1000.0 << " km" << std::endl;
     * @endcode
     */
    [[nodiscard]] double MeasureDistance(
        const coordinates::Geographic& from,
        const coordinates::Geographic& to) const noexcept;

    /**
     * @brief Calculate bearing from one location to another
     *
     * @param from Start location
     * @param to End location
     * @return Bearing in degrees [0, 360), where 0° is North
     *
     * @example
     * @code
     * double bearing = interaction->CalculateBearing(nyc, london);
     * std::cout << "Travel " << bearing << "° from NYC to reach London" << std::endl;
     * @endcode
     */
    [[nodiscard]] double CalculateBearing(
        const coordinates::Geographic& from,
        const coordinates::Geographic& to) const noexcept;

    // ========================================================================
    // Camera Information (Read-Only)
    // ========================================================================

    /**
     * @brief Get current camera position in geographic terms
     *
     * Returns the geographic location where the camera is positioned.
     * For orbital camera, this is the point in space where camera orbits from.
     *
     * @return Camera location (lat, lon, altitude in meters)
     */
    [[nodiscard]] coordinates::Geographic GetCameraLocation() const;

    /**
     * @brief Get geographic point at center of view
     *
     * Returns the location on Earth's surface that the camera is looking at.
     * This is the point at the center of the screen.
     *
     * @return Geographic point at view center
     */
    [[nodiscard]] coordinates::Geographic GetCameraTarget() const;

    /**
     * @brief Get camera distance from Earth's surface
     *
     * @return Distance in meters
     */
    [[nodiscard]] double GetCameraAltitude() const;

    // ========================================================================
    // Camera Control (Navigation)
    // ========================================================================

    /**
     * @brief Move camera to look at a location
     *
     * Smoothly animates camera to view the specified location.
     *
     * @param location Geographic coordinates to view
     * @param altitude Camera altitude in meters (optional)
     * @param duration Animation duration in seconds
     *
     * @example
     * @code
     * coordinates::Geographic tokyo(35.6762, 139.6503, 0.0);
     * interaction->FlyToLocation(tokyo, 5000000.0, 2.0);  // Fly to Tokyo, 5000km altitude, 2 sec
     * @endcode
     */
    void FlyToLocation(
        const coordinates::Geographic& location,
        double altitude = 10000000.0,  // Default: 10,000 km
        double duration = 1.5);

    /**
     * @brief Set camera to look at location immediately (no animation)
     *
     * @param location Geographic coordinates to view
     * @param altitude Camera altitude in meters
     */
    void SetCameraView(
        const coordinates::Geographic& location,
        double altitude = 10000000.0);

    // ========================================================================
    // Viewport Information
    // ========================================================================

    /**
     * @brief Get current viewport dimensions
     *
     * @return {width, height} in pixels
     */
    [[nodiscard]] std::pair<int, int> GetViewportSize() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace api
} // namespace earth_map
