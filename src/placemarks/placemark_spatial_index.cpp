#include "placemark_spatial_index.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <utility>

namespace earth_map::placemarks::internal {
namespace {

EcefBounds EmptyBounds() noexcept {
    const double infinity = std::numeric_limits<double>::infinity();
    return {{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
}

void EncloseGeodetic(EcefBounds& bounds, const geodesy::GeodeticPosition& position) noexcept {
    bounds.Enclose(geodesy::Wgs84Ellipsoid::ToEcef(position).meters);
}

}  // namespace

bool EcefBounds::IsValid() const noexcept {
    return std::isfinite(minimum_meters.x) && std::isfinite(minimum_meters.y) &&
           std::isfinite(minimum_meters.z) && std::isfinite(maximum_meters.x) &&
           std::isfinite(maximum_meters.y) && std::isfinite(maximum_meters.z) &&
           minimum_meters.x <= maximum_meters.x && minimum_meters.y <= maximum_meters.y &&
           minimum_meters.z <= maximum_meters.z;
}

glm::dvec3 EcefBounds::Center() const noexcept {
    return (minimum_meters + maximum_meters) * 0.5;
}

void EcefBounds::Enclose(const glm::dvec3& point_meters) noexcept {
    minimum_meters = glm::min(minimum_meters, point_meters);
    maximum_meters = glm::max(maximum_meters, point_meters);
}

void EcefBounds::Enclose(const EcefBounds& other) noexcept {
    if (!other.IsValid()) {
        return;
    }
    Enclose(other.minimum_meters);
    Enclose(other.maximum_meters);
}

void PlacemarkSpatialIndex::Rebuild(const PlacemarkSnapshot& snapshot) {
    if (snapshot.Revision() == revision_) {
        return;
    }

    entries_.clear();
    nodes_.clear();
    root_node_ = -1;
    entries_.reserve(snapshot.Features().size());
    for (const auto& [id, feature] : snapshot.Features()) {
        const EcefBounds bounds = FeatureBounds(feature);
        if (bounds.IsValid()) {
            entries_.push_back({id, bounds});
        }
    }

    if (!entries_.empty()) {
        root_node_ = BuildNode(0, entries_.size());
    }
    revision_ = snapshot.Revision();
}

std::vector<PlacemarkSpatialCandidate> PlacemarkSpatialIndex::Query(
    const EcefFrustum& frustum) const {
    std::vector<PlacemarkSpatialCandidate> results;
    if (root_node_ >= 0) {
        QueryNode(root_node_, frustum, results);
    }
    return results;
}

EcefBounds PlacemarkSpatialIndex::FeatureBounds(const PlacemarkFeature& feature) noexcept {
    EcefBounds bounds = EmptyBounds();
    std::visit([&bounds](const auto& value) {
        using Feature = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Feature, PointPlacemark>) {
            EncloseGeodetic(bounds, value.position);
        } else if constexpr (std::is_same_v<Feature, LineStringPlacemark>) {
            for (const auto& position : value.positions) {
                EncloseGeodetic(bounds, position);
            }
        } else if constexpr (std::is_same_v<Feature, PolygonPlacemark>) {
            for (const auto& position : value.outer_boundary) {
                EncloseGeodetic(bounds, position);
            }
            for (const auto& hole : value.holes) {
                for (const auto& position : hole) {
                    EncloseGeodetic(bounds, position);
                }
            }
        } else {
            EncloseGeodetic(bounds, value.anchor);
        }
    }, feature);
    return bounds;
}

bool PlacemarkSpatialIndex::Intersects(
    const EcefBounds& bounds, const EcefFrustum& frustum) noexcept {
    for (const EcefPlane& plane : frustum.planes) {
        const glm::dvec3 positive_vertex{
            plane.normal.x >= 0.0 ? bounds.maximum_meters.x : bounds.minimum_meters.x,
            plane.normal.y >= 0.0 ? bounds.maximum_meters.y : bounds.minimum_meters.y,
            plane.normal.z >= 0.0 ? bounds.maximum_meters.z : bounds.minimum_meters.z,
        };
        if (glm::dot(plane.normal, positive_vertex) + plane.distance < 0.0) {
            return false;
        }
    }
    return true;
}

std::int32_t PlacemarkSpatialIndex::BuildNode(
    std::size_t first_entry, std::size_t entry_count) {
    EcefBounds bounds = EmptyBounds();
    for (std::size_t index = first_entry; index < first_entry + entry_count; ++index) {
        bounds.Enclose(entries_[index].bounds);
    }

    const std::int32_t node_index = static_cast<std::int32_t>(nodes_.size());
    nodes_.push_back({bounds, first_entry, entry_count});
    if (entry_count <= kLeafEntryCount) {
        return node_index;
    }

    const glm::dvec3 extent = bounds.maximum_meters - bounds.minimum_meters;
    const std::size_t axis = extent.y > extent.x && extent.y >= extent.z ? 1U :
                             extent.z > extent.x && extent.z > extent.y ? 2U : 0U;
    const std::size_t middle = first_entry + entry_count / 2U;
    std::nth_element(
        entries_.begin() + static_cast<std::ptrdiff_t>(first_entry),
        entries_.begin() + static_cast<std::ptrdiff_t>(middle),
        entries_.begin() + static_cast<std::ptrdiff_t>(first_entry + entry_count),
        [axis](const Entry& first, const Entry& second) {
            return first.bounds.Center()[axis] < second.bounds.Center()[axis];
        });

    const std::int32_t left = BuildNode(first_entry, middle - first_entry);
    const std::int32_t right = BuildNode(middle, first_entry + entry_count - middle);
    nodes_[node_index].left_child = left;
    nodes_[node_index].right_child = right;
    return node_index;
}

void PlacemarkSpatialIndex::QueryNode(
    std::int32_t node_index,
    const EcefFrustum& frustum,
    std::vector<PlacemarkSpatialCandidate>& results) const {
    const Node& node = nodes_[static_cast<std::size_t>(node_index)];
    if (!Intersects(node.bounds, frustum)) {
        return;
    }

    if (!node.IsLeaf()) {
        QueryNode(node.left_child, frustum, results);
        QueryNode(node.right_child, frustum, results);
        return;
    }

    for (std::size_t index = node.first_entry; index < node.first_entry + node.entry_count; ++index) {
        const Entry& entry = entries_[index];
        if (Intersects(entry.bounds, frustum)) {
            results.push_back({entry.id, entry.bounds});
        }
    }
}

}  // namespace earth_map::placemarks::internal
