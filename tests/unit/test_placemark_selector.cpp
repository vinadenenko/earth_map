#include <gtest/gtest.h>

#include <earth_map/placemarks/placemark_layer.h>

#include "../../src/placemarks/placemark_selector.h"

namespace earth_map::placemarks::internal::tests {
namespace {

geodesy::GeodeticPosition Position(double latitude, double longitude) {
    return {latitude, longitude, 0.0};
}

PointPlacemark Point(PlacemarkId id, double latitude, double longitude, std::int32_t priority = 0) {
    PointPlacemark point;
    point.metadata.id = id;
    point.metadata.display_priority = priority;
    point.position = Position(latitude, longitude);
    return point;
}

EcefFrustum WorldFrustum() {
    constexpr double kExtent = 100'000'000.0;
    EcefFrustum frustum;
    frustum.planes[0] = {{1.0, 0.0, 0.0}, kExtent};
    frustum.planes[1] = {{-1.0, 0.0, 0.0}, kExtent};
    frustum.planes[2] = {{0.0, 1.0, 0.0}, kExtent};
    frustum.planes[3] = {{0.0, -1.0, 0.0}, kExtent};
    frustum.planes[4] = {{0.0, 0.0, 1.0}, kExtent};
    frustum.planes[5] = {{0.0, 0.0, -1.0}, kExtent};
    return frustum;
}

PlacemarkSelectionInput InputAt(double latitude, double longitude, double height) {
    return {WorldFrustum(), geodesy::Wgs84Ellipsoid::ToEcef({latitude, longitude, height})};
}

}  // namespace

TEST(PlacemarkSelectorTest, CullsPointPlacemarkBehindWgs84Horizon) {
    const auto layer = PlacemarkLayer::Create();
    ASSERT_TRUE(layer->Apply({{}, {Point(1, 0.0, 0.0), Point(2, 0.0, 3.141592653589793)}}).applied);

    PlacemarkSelector selector;
    const auto selected = selector.Select(layer->Snapshot(), InputAt(0.0, 0.0, 1'000'000.0));

    ASSERT_EQ(selected.size(), 1U);
    EXPECT_EQ(selected.front().id, 1U);
}

TEST(PlacemarkSelectorTest, ProducesStablePriorityThenIdOrdering) {
    const auto layer = PlacemarkLayer::Create();
    ASSERT_TRUE(layer->Apply({{}, {
        Point(3, 0.0, 0.0, 0), Point(2, 0.01, 0.0, 5), Point(1, 0.02, 0.0, 5)}}).applied);

    PlacemarkSelector selector;
    const auto selected = selector.Select(layer->Snapshot(), InputAt(0.0, 0.0, 1'000'000.0));

    ASSERT_EQ(selected.size(), 3U);
    EXPECT_EQ(selected[0].id, 1U);
    EXPECT_EQ(selected[1].id, 2U);
    EXPECT_EQ(selected[2].id, 3U);
}

}  // namespace earth_map::placemarks::internal::tests
