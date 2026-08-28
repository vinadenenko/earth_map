#include "placemark_selector.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace earth_map::placemarks::internal {
namespace {

bool IsPointOrCustom(const PlacemarkFeature& feature) noexcept {
    return std::holds_alternative<PointPlacemark>(feature) ||
           std::holds_alternative<CustomPlacemark>(feature);
}

}  // namespace

std::vector<SelectedPlacemark> PlacemarkSelector::Select(
    const PlacemarkSnapshot& snapshot,
    const PlacemarkSelectionInput& input) {
    spatial_index_.Rebuild(snapshot);

    std::vector<SelectedPlacemark> selected;
    for (const PlacemarkSpatialCandidate& candidate : spatial_index_.Query(input.frustum)) {
        const double distance = DistanceToBounds(input.camera.meters, candidate.bounds);
        if (distance > input.maximum_distance_meters) {
            continue;
        }

        const auto feature = snapshot.Features().find(candidate.id);
        if (feature == snapshot.Features().end()) {
            continue;
        }

        const geodesy::EcefPosition anchor = AnchorOf(feature->second);
        if (input.cull_point_and_custom_horizon && IsPointOrCustom(feature->second) &&
            IsOccludedByWgs84Ellipsoid(input.camera, anchor)) {
            continue;
        }

        const auto [visible, display_priority] = std::visit([](const auto& value) {
            return std::pair{value.metadata.visible, value.metadata.display_priority};
        }, feature->second);
        if (!visible) {
            continue;
        }
        selected.push_back({candidate.id, KindOf(feature->second), anchor, distance,
                            display_priority});
    }

    std::sort(selected.begin(), selected.end(), [](const SelectedPlacemark& first,
                                                    const SelectedPlacemark& second) {
        return first.display_priority != second.display_priority
            ? first.display_priority > second.display_priority
            : first.id < second.id;
    });
    return selected;
}

PlacemarkFeatureKind PlacemarkSelector::KindOf(const PlacemarkFeature& feature) noexcept {
    return std::visit([](const auto& value) {
        using Feature = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Feature, PointPlacemark>) {
            return PlacemarkFeatureKind::Point;
        } else if constexpr (std::is_same_v<Feature, LineStringPlacemark>) {
            return PlacemarkFeatureKind::LineString;
        } else if constexpr (std::is_same_v<Feature, PolygonPlacemark>) {
            return PlacemarkFeatureKind::Polygon;
        } else {
            return PlacemarkFeatureKind::Custom;
        }
    }, feature);
}

geodesy::EcefPosition PlacemarkSelector::AnchorOf(const PlacemarkFeature& feature) noexcept {
    return std::visit([](const auto& value) {
        using Feature = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Feature, PointPlacemark>) {
            return geodesy::Wgs84Ellipsoid::ToEcef(value.position);
        } else if constexpr (std::is_same_v<Feature, CustomPlacemark>) {
            return geodesy::Wgs84Ellipsoid::ToEcef(value.anchor);
        } else {
            glm::dvec3 sum{0.0};
            std::size_t count = 0;
            const auto add = [&sum, &count](const geodesy::GeodeticPosition& position) {
                sum += geodesy::Wgs84Ellipsoid::ToEcef(position).meters;
                ++count;
            };
            if constexpr (std::is_same_v<Feature, LineStringPlacemark>) {
                for (const auto& position : value.positions) {
                    add(position);
                }
            } else {
                for (const auto& position : value.outer_boundary) {
                    add(position);
                }
                for (const auto& hole : value.holes) {
                    for (const auto& position : hole) {
                        add(position);
                    }
                }
            }
            return geodesy::EcefPosition{sum / static_cast<double>(count)};
        }
    }, feature);
}

double PlacemarkSelector::DistanceToBounds(
    const glm::dvec3& point, const EcefBounds& bounds) noexcept {
    const glm::dvec3 nearest = glm::clamp(point, bounds.minimum_meters, bounds.maximum_meters);
    return glm::length(point - nearest);
}

bool PlacemarkSelector::IsOccludedByWgs84Ellipsoid(
    const geodesy::EcefPosition& camera,
    const geodesy::EcefPosition& target) noexcept {
    const glm::dvec3 direction = target.meters - camera.meters;
    const double a2 = geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters *
        geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters;
    const double b2 = geodesy::Wgs84Ellipsoid::kSemiMinorAxisMeters *
        geodesy::Wgs84Ellipsoid::kSemiMinorAxisMeters;
    const auto ellipsoid_dot = [a2, b2](const glm::dvec3& first,
                                        const glm::dvec3& second) noexcept {
        return (first.x * second.x + first.y * second.y) / a2 +
               (first.z * second.z) / b2;
    };

    const double quadratic_a = ellipsoid_dot(direction, direction);
    const double quadratic_b = 2.0 * ellipsoid_dot(camera.meters, direction);
    const double quadratic_c = ellipsoid_dot(camera.meters, camera.meters) - 1.0;
    const double discriminant = quadratic_b * quadratic_b - 4.0 * quadratic_a * quadratic_c;
    if (quadratic_a <= 0.0 || discriminant < 0.0) {
        return false;
    }

    const double root = std::sqrt(discriminant);
    const double first_intersection = (-quadratic_b - root) / (2.0 * quadratic_a);
    const double second_intersection = (-quadratic_b + root) / (2.0 * quadratic_a);
    constexpr double kSegmentTolerance = 1e-9;
    return (first_intersection > kSegmentTolerance && first_intersection < 1.0 - kSegmentTolerance) ||
           (second_intersection > kSegmentTolerance && second_intersection < 1.0 - kSegmentTolerance);
}

}  // namespace earth_map::placemarks::internal
