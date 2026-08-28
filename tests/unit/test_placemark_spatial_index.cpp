#include <gtest/gtest.h>

#include <earth_map/placemarks/placemark_layer.h>

#include "../../src/placemarks/placemark_spatial_index.h"

namespace earth_map::placemarks::internal::tests {
namespace {

geodesy::GeodeticPosition Position(double latitude, double longitude) {
    return {latitude, longitude, 0.0};
}

PointPlacemark Point(PlacemarkId id, double latitude, double longitude) {
    PointPlacemark point;
    point.metadata.id = id;
    point.position = Position(latitude, longitude);
    return point;
}

EcefFrustum CubeAround(const glm::dvec3& center, double half_extent) {
    EcefFrustum frustum;
    frustum.planes[0] = {{1.0, 0.0, 0.0}, -(center.x - half_extent)};
    frustum.planes[1] = {{-1.0, 0.0, 0.0}, center.x + half_extent};
    frustum.planes[2] = {{0.0, 1.0, 0.0}, -(center.y - half_extent)};
    frustum.planes[3] = {{0.0, -1.0, 0.0}, center.y + half_extent};
    frustum.planes[4] = {{0.0, 0.0, 1.0}, -(center.z - half_extent)};
    frustum.planes[5] = {{0.0, 0.0, -1.0}, center.z + half_extent};
    return frustum;
}

}  // namespace

TEST(PlacemarkSpatialIndexTest, QueriesOnlyFeaturesIntersectingEcefFrustum) {
    const auto layer = PlacemarkLayer::Create();
    ASSERT_TRUE(layer->Apply({{}, {Point(1, 0.0, 0.0), Point(2, 0.0, 1.0)}}).applied);

    PlacemarkSpatialIndex index;
    index.Rebuild(layer->Snapshot());
    ASSERT_EQ(index.Size(), 2U);

    const glm::dvec3 first_ecef = geodesy::Wgs84Ellipsoid::ToEcef(Position(0.0, 0.0)).meters;
    const auto candidates = index.Query(CubeAround(first_ecef, 10'000.0));

    ASSERT_EQ(candidates.size(), 1U);
    EXPECT_EQ(candidates.front().id, 1U);
}

TEST(PlacemarkSpatialIndexTest, RebuildsOnlyWhenSnapshotRevisionChanges) {
    const auto layer = PlacemarkLayer::Create();
    ASSERT_TRUE(layer->Apply({{}, {Point(1, 0.0, 0.0)}}).applied);

    PlacemarkSpatialIndex index;
    const PlacemarkSnapshot first_snapshot = layer->Snapshot();
    index.Rebuild(first_snapshot);
    EXPECT_EQ(index.Revision(), first_snapshot.Revision());

    index.Rebuild(first_snapshot);
    EXPECT_EQ(index.Size(), 1U);

    ASSERT_TRUE(layer->Apply({{}, {Point(2, 0.2, 0.3)}}).applied);
    index.Rebuild(layer->Snapshot());
    EXPECT_EQ(index.Size(), 2U);
}

}  // namespace earth_map::placemarks::internal::tests
