/**
 * @file placemark_spatial_index.h
 * @brief Private ECEF BVH used by future placemark selection/rendering.
 */

#pragma once

#include <earth_map/placemarks/placemark_layer.h>

#include <array>
#include <cstdint>
#include <vector>

namespace earth_map::placemarks::internal {

struct EcefBounds final {
    glm::dvec3 minimum_meters{0.0};
    glm::dvec3 maximum_meters{0.0};

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] glm::dvec3 Center() const noexcept;
    void Enclose(const glm::dvec3& point_meters) noexcept;
    void Enclose(const EcefBounds& other) noexcept;
};

/** Plane normal points into the visible half-space. */
struct EcefPlane final {
    glm::dvec3 normal{0.0, 0.0, 1.0};
    double distance = 0.0;
};

struct EcefFrustum final {
    std::array<EcefPlane, 6> planes;
};

struct PlacemarkSpatialCandidate final {
    PlacemarkId id = 0;
    EcefBounds bounds;
};

/**
 * Renderer-private, immutable-snapshot BVH.
 *
 * Rebuild is called only when a PlacemarkSnapshot revision changes. Query is
 * allocation-bounded by its result vector and never reads application-owned
 * mutable feature state.
 */
class PlacemarkSpatialIndex final {
public:
    void Rebuild(const PlacemarkSnapshot& snapshot);

    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }
    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::vector<PlacemarkSpatialCandidate> Query(
        const EcefFrustum& frustum) const;

private:
    struct Entry final {
        PlacemarkId id = 0;
        EcefBounds bounds;
    };

    struct Node final {
        EcefBounds bounds;
        std::size_t first_entry = 0;
        std::size_t entry_count = 0;
        std::int32_t left_child = -1;
        std::int32_t right_child = -1;

        [[nodiscard]] bool IsLeaf() const noexcept { return left_child < 0; }
    };

    [[nodiscard]] static EcefBounds FeatureBounds(const PlacemarkFeature& feature) noexcept;
    [[nodiscard]] static bool Intersects(
        const EcefBounds& bounds, const EcefFrustum& frustum) noexcept;
    [[nodiscard]] std::int32_t BuildNode(std::size_t first_entry, std::size_t entry_count);
    void QueryNode(std::int32_t node_index,
                   const EcefFrustum& frustum,
                   std::vector<PlacemarkSpatialCandidate>& results) const;

    static constexpr std::size_t kLeafEntryCount = 16;

    std::uint64_t revision_ = 0;
    std::int32_t root_node_ = -1;
    std::vector<Entry> entries_;
    std::vector<Node> nodes_;
};

}  // namespace earth_map::placemarks::internal
