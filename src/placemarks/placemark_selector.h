/**
 * @file placemark_selector.h
 * @brief Private camera-dependent placemark selection over the ECEF BVH.
 */

#pragma once

#include "placemark_spatial_index.h"

#include <limits>
#include <vector>

namespace earth_map::placemarks::internal {

enum class PlacemarkFeatureKind : std::uint8_t {
    Point,
    LineString,
    Polygon,
    Custom,
};

struct PlacemarkSelectionInput final {
    EcefFrustum frustum;
    geodesy::EcefPosition camera;
    double maximum_distance_meters = std::numeric_limits<double>::infinity();
    bool cull_point_and_custom_horizon = true;
};

struct SelectedPlacemark final {
    PlacemarkId id = 0;
    PlacemarkFeatureKind kind = PlacemarkFeatureKind::Point;
    geodesy::EcefPosition anchor;
    double distance_meters = 0.0;
    std::int32_t display_priority = 0;
};

/**
 * Private bridge between immutable layer snapshots and future GPU batching.
 *
 * This owns no feature data. Its result is stable by priority then ID, which
 * makes subsequent screen-grid clustering deterministic.
 */
class PlacemarkSelector final {
public:
    [[nodiscard]] std::vector<SelectedPlacemark> Select(
        const PlacemarkSnapshot& snapshot,
        const PlacemarkSelectionInput& input);

private:
    [[nodiscard]] static PlacemarkFeatureKind KindOf(const PlacemarkFeature& feature) noexcept;
    [[nodiscard]] static geodesy::EcefPosition AnchorOf(const PlacemarkFeature& feature) noexcept;
    [[nodiscard]] static double DistanceToBounds(
        const glm::dvec3& point, const EcefBounds& bounds) noexcept;
    [[nodiscard]] static bool IsOccludedByWgs84Ellipsoid(
        const geodesy::EcefPosition& camera,
        const geodesy::EcefPosition& target) noexcept;

    PlacemarkSpatialIndex spatial_index_;
};

}  // namespace earth_map::placemarks::internal
