#include <gtest/gtest.h>

#include <earth_map/renderer/geographic_quadtree.h>

#include <numbers>

namespace earth_map::renderer {
namespace {

imagery::ImageTileKey MakeKey(
    std::uint32_t level,
    std::uint32_t column,
    std::uint32_t row) {
    return {"test-source", "WebMercatorQuad", {level, column, row}};
}

}  // namespace

TEST(GeographicQuadtreePatchTest, WebMercatorTileMapsToWgs84GeodeticBounds) {
    const imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    const auto patch = MakeGeographicQuadtreePatch(matrix_set, MakeKey(1, 1, 1));

    ASSERT_TRUE(patch.has_value());
    EXPECT_NEAR(patch->bounds.west_longitude_radians, 0.0, 1e-12);
    EXPECT_NEAR(patch->bounds.east_longitude_radians,
                std::numbers::pi_v<double>, 1e-12);
    EXPECT_NEAR(patch->bounds.north_latitude_radians, 0.0, 1e-12);
    EXPECT_NEAR(patch->bounds.south_latitude_radians,
                -imagery::TileMatrixSet::kWebMercatorMaxLatitudeRadians, 1e-12);
}

TEST(GeographicQuadtreePatchTest, TmsRowOrderUsesTheSamePhysicalBounds) {
    imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    matrix_set.row_order = imagery::TileRowOrder::SouthToNorth;

    const auto patch = MakeGeographicQuadtreePatch(matrix_set, MakeKey(1, 1, 0));

    ASSERT_TRUE(patch.has_value());
    EXPECT_NEAR(patch->bounds.north_latitude_radians, 0.0, 1e-12);
    EXPECT_NEAR(patch->bounds.south_latitude_radians,
                -imagery::TileMatrixSet::kWebMercatorMaxLatitudeRadians, 1e-12);
}

TEST(GeographicQuadtreePatchTest, PatchUvMapsToWgs84EcefInsteadOfASphereApproximation) {
    const imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    const auto patch = MakeGeographicQuadtreePatch(matrix_set, MakeKey(1, 1, 1));
    ASSERT_TRUE(patch.has_value());

    const auto geodetic = PatchLocalToGeodetic(*patch, {0.0, 0.0});
    const auto ecef = PatchLocalToEcef(*patch, {0.0, 0.0});

    ASSERT_TRUE(geodetic.has_value());
    ASSERT_TRUE(ecef.has_value());
    EXPECT_NEAR(geodetic->latitude_radians, 0.0, 1e-12);
    EXPECT_NEAR(geodetic->longitude_radians, 0.0, 1e-12);
    EXPECT_NEAR(ecef->meters.x, geodesy::Wgs84Ellipsoid::kSemiMajorAxisMeters, 1e-6);
    EXPECT_NEAR(ecef->meters.y, 0.0, 1e-6);
    EXPECT_NEAR(ecef->meters.z, 0.0, 1e-6);
}

TEST(GeographicQuadtreePatchTest, ResolvesClosestResidentAncestorWithExactUvTransform) {
    const imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    const auto patch = MakeGeographicQuadtreePatch(matrix_set, MakeKey(5, 23, 11));
    ASSERT_TRUE(patch.has_value());

    const auto resolved = ResolveResidentImageryPatch(
        *patch,
        4,
        [](const imagery::ImageTileKey& key) -> std::optional<std::uint16_t> {
            if (key.address == imagery::ImageTileAddress{3, 5, 2}) {
                return 17;
            }
            return std::nullopt;
        });

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->resident_key.address, (imagery::ImageTileAddress{3, 5, 2}));
    EXPECT_EQ(resolved->texture_layer, 17);
    EXPECT_EQ(resolved->ancestor_levels, 2U);
    EXPECT_DOUBLE_EQ(resolved->uv_scale.x, 0.25);
    EXPECT_DOUBLE_EQ(resolved->uv_scale.y, 0.25);
    EXPECT_DOUBLE_EQ(resolved->uv_offset.x, 0.75);
    EXPECT_DOUBLE_EQ(resolved->uv_offset.y, 0.75);
}

TEST(GeographicQuadtreePatchTest, DoesNotInventAnImageryFallback) {
    const imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    const auto patch = MakeGeographicQuadtreePatch(matrix_set, MakeKey(5, 23, 11));
    ASSERT_TRUE(patch.has_value());

    EXPECT_FALSE(ResolveResidentImageryPatch(
        *patch,
        4,
        [](const imagery::ImageTileKey&) -> std::optional<std::uint16_t> {
            return std::nullopt;
        }).has_value());
}

TEST(GeographicQuadtreePatchGridTest, GridIsSharedTopologyWithUnitCoordinates) {
    const auto grid = MakeGeographicPatchGrid(2);

    ASSERT_TRUE(grid.has_value());
    EXPECT_EQ(grid->local_coordinates.size(), 9U);
    EXPECT_EQ(grid->indices.size(), 24U);
    EXPECT_FLOAT_EQ(grid->local_coordinates.front().x, 0.0F);
    EXPECT_FLOAT_EQ(grid->local_coordinates.front().y, 0.0F);
    EXPECT_FLOAT_EQ(grid->local_coordinates.back().x, 1.0F);
    EXPECT_FLOAT_EQ(grid->local_coordinates.back().y, 1.0F);
    EXPECT_EQ(grid->indices,
              (std::vector<std::uint32_t>{0, 3, 1, 1, 3, 4,
                                          1, 4, 2, 2, 4, 5,
                                          3, 6, 4, 4, 6, 7,
                                          4, 7, 5, 5, 7, 8}));
}

TEST(GeographicQuadtreePatchGridTest, RejectsInvalidGridResolution) {
    EXPECT_FALSE(MakeGeographicPatchGrid(0).has_value());
    EXPECT_FALSE(MakeGeographicPatchGrid(257).has_value());
}

TEST(GeographicQuadtreeSelectionTest, RefinesTheFullSourceMatrixToTargetLevel) {
    const imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    const GeographicQuadtreeSelectionConfig config{
        {{{-std::numbers::pi_v<double>,
           -imagery::TileMatrixSet::kWebMercatorMaxLatitudeRadians,
           std::numbers::pi_v<double>,
           imagery::TileMatrixSet::kWebMercatorMaxLatitudeRadians}}},
        2,
        16,
    };

    const std::vector<imagery::ImageTileKey> selected =
        SelectVisibleGeographicQuadtreeLeaves(matrix_set, "test-source", config);

    ASSERT_EQ(selected.size(), 16U);
    for (const imagery::ImageTileKey& key : selected) {
        EXPECT_EQ(key.imagery_source_id, "test-source");
        EXPECT_EQ(key.matrix_set_id, matrix_set.id);
        EXPECT_EQ(key.address.level, 2U);
    }
}

TEST(GeographicQuadtreeSelectionTest, KeepsACompleteCoarserFrontierAtTheLeafBudget) {
    const imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    const GeographicQuadtreeSelectionConfig config{
        {{{-std::numbers::pi_v<double>,
           -imagery::TileMatrixSet::kWebMercatorMaxLatitudeRadians,
           std::numbers::pi_v<double>,
           imagery::TileMatrixSet::kWebMercatorMaxLatitudeRadians}}},
        5,
        16,
    };

    const std::vector<imagery::ImageTileKey> selected =
        SelectVisibleGeographicQuadtreeLeaves(matrix_set, "test-source", config);

    ASSERT_EQ(selected.size(), 16U);
    for (const imagery::ImageTileKey& key : selected) {
        EXPECT_EQ(key.address.level, 2U);
    }
}

TEST(GeographicQuadtreeSelectionTest, SelectsOnlyIntersectingLeaves) {
    const imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    const GeographicQuadtreeSelectionConfig config{
        {{{0.1,
           0.1,
           std::numbers::pi_v<double> - 0.1,
           imagery::TileMatrixSet::kWebMercatorMaxLatitudeRadians - 0.1}}},
        2,
        16,
    };

    const std::vector<imagery::ImageTileKey> selected =
        SelectVisibleGeographicQuadtreeLeaves(matrix_set, "test-source", config);

    ASSERT_EQ(selected.size(), 4U);
    for (const imagery::ImageTileKey& key : selected) {
        EXPECT_EQ(key.address.level, 2U);
        EXPECT_GE(key.address.column, 2U);
        EXPECT_LE(key.address.column, 3U);
        EXPECT_LE(key.address.row, 1U);
    }
}

TEST(GeographicQuadtreeSelectionTest, PreservesTmsRowsForTheSamePhysicalView) {
    imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    matrix_set.row_order = imagery::TileRowOrder::SouthToNorth;
    const GeographicQuadtreeSelectionConfig config{
        {{{0.1,
           0.1,
           std::numbers::pi_v<double> - 0.1,
           imagery::TileMatrixSet::kWebMercatorMaxLatitudeRadians - 0.1}}},
        2,
        16,
    };

    const std::vector<imagery::ImageTileKey> selected =
        SelectVisibleGeographicQuadtreeLeaves(matrix_set, "test-source", config);

    ASSERT_EQ(selected.size(), 4U);
    for (const imagery::ImageTileKey& key : selected) {
        EXPECT_EQ(key.address.level, 2U);
        EXPECT_GE(key.address.row, 2U);
        EXPECT_LE(key.address.row, 3U);
    }
}

}  // namespace earth_map::renderer
