#include <gtest/gtest.h>

#include <earth_map/placemarks/placemark_layer.h>

namespace earth_map::placemarks::tests {
namespace {

geodesy::GeodeticPosition Position(double latitude, double longitude, double height = 0.0) {
    return {latitude, longitude, height};
}

PointPlacemark Point(PlacemarkId id) {
    PointPlacemark point;
    point.metadata.id = id;
    point.position = Position(0.5, -1.0, 25.0);
    point.icon.icon_key = "pin";
    return point;
}

}  // namespace

TEST(PlacemarkLayerTest, AppliesAtomicChangesAndReturnsImmutableSnapshots) {
    const auto layer = PlacemarkLayer::Create();
    const PlacemarkFeature point = Point(42);

    const PlacemarkApplyResult applied = layer->Apply({{}, {point}});
    ASSERT_TRUE(applied.applied);
    EXPECT_EQ(applied.revision, 1U);

    const PlacemarkSnapshot first_snapshot = layer->Snapshot();
    ASSERT_EQ(first_snapshot.Features().size(), 1U);
    EXPECT_EQ(first_snapshot.Revision(), 1U);

    const PlacemarkApplyResult removed = layer->Apply({{42}, {}});
    ASSERT_TRUE(removed.applied);
    EXPECT_EQ(removed.revision, 2U);
    EXPECT_TRUE(layer->Snapshot().Empty());
    EXPECT_EQ(first_snapshot.Features().size(), 1U);
}

TEST(PlacemarkLayerTest, RejectsInvalidChangeSetWithoutPartialMutation) {
    const auto layer = PlacemarkLayer::Create();
    const PlacemarkFeature valid = Point(1);
    PointPlacemark invalid = Point(2);
    invalid.position.latitude_radians = 2.0;

    const PlacemarkApplyResult result = layer->Apply({{}, {valid, invalid}});
    EXPECT_FALSE(result.applied);
    ASSERT_EQ(result.rejected_upsert_ids.size(), 1U);
    EXPECT_EQ(result.rejected_upsert_ids.front(), 2U);
    EXPECT_TRUE(layer->Snapshot().Empty());
}

TEST(PlacemarkLayerTest, SupportsFutureLinePolygonAndCustomFeatureKinds) {
    const auto layer = PlacemarkLayer::Create();

    LineStringPlacemark line;
    line.metadata.id = 1;
    line.positions = {Position(0.1, 0.2), Position(0.2, 0.3)};

    PolygonPlacemark polygon;
    polygon.metadata.id = 2;
    polygon.outer_boundary = {
        Position(0.1, 0.2), Position(0.2, 0.2), Position(0.2, 0.3)};

    CustomPlacemark custom;
    custom.metadata.id = 3;
    custom.anchor = Position(0.3, 0.4);
    custom.type_key = "application.sensor";
    custom.metadata.user_properties.emplace("sensor-id", "A-17");

    const PlacemarkApplyResult result = layer->Apply({{}, {line, polygon, custom}});
    ASSERT_TRUE(result.applied);
    const PlacemarkSnapshot snapshot = layer->Snapshot();
    ASSERT_EQ(snapshot.Features().size(), 3U);
    EXPECT_TRUE(snapshot.Features().contains(1));
    EXPECT_TRUE(snapshot.Features().contains(2));
    EXPECT_TRUE(snapshot.Features().contains(3));
}

}  // namespace earth_map::placemarks::tests
