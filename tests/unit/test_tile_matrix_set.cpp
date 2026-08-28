#include <gtest/gtest.h>

#include <earth_map/imagery/tile_matrix_set.h>
#include <earth_map/data/tile_loader.h>

#include <numbers>
#include <unordered_set>

namespace earth_map::imagery {
namespace {

constexpr double DegreesToRadians(double degrees) {
    return degrees * std::numbers::pi_v<double> / 180.0;
}

ImageTileKey MakeKey(std::uint32_t level, std::uint32_t column, std::uint32_t row) {
    return ImageTileKey{"osm", "WebMercatorQuad", {level, column, row}};
}

}  // namespace

TEST(TileMatrixSetTest, WebMercatorXyzDeclaresAValidSourceMatrix) {
    const TileMatrixSet matrix_set = TileMatrixSet::WebMercatorXYZ();

    EXPECT_TRUE(matrix_set.IsValid());
    EXPECT_EQ(matrix_set.MatrixDimension(0), 1U);
    EXPECT_EQ(matrix_set.MatrixDimension(13), 8192U);
    EXPECT_EQ(matrix_set.MatrixDimension(21), 2097152U);
    EXPECT_EQ(matrix_set.MatrixDimension(23), 0U);
}

TEST(TileMatrixSetTest, WebMercatorMapsEquatorPrimeMeridianToTheNorthwestQuadrant) {
    const TileMatrixSet matrix_set = TileMatrixSet::WebMercatorXYZ();
    const auto address = matrix_set.GeodeticToTile({0.0, 0.0, 0.0}, 3);

    ASSERT_TRUE(address.has_value());
    EXPECT_EQ(address->column, 4U);
    EXPECT_EQ(address->row, 4U);
}

TEST(TileMatrixSetTest, WebMercatorUsesIntegerHierarchyAtHighZoom) {
    const TileMatrixSet matrix_set = TileMatrixSet::WebMercatorXYZ();
    const geodesy::GeodeticPosition yerevan{
        DegreesToRadians(40.1872), DegreesToRadians(44.5152), 0.0};

    const auto z13 = matrix_set.GeodeticToTile(yerevan, 13);
    const auto z18 = matrix_set.GeodeticToTile(yerevan, 18);
    const auto z21 = matrix_set.GeodeticToTile(yerevan, 21);

    ASSERT_TRUE(z13.has_value());
    ASSERT_TRUE(z18.has_value());
    ASSERT_TRUE(z21.has_value());
    EXPECT_EQ(z18->column >> 5U, z13->column);
    EXPECT_EQ(z18->row >> 5U, z13->row);
    EXPECT_EQ(z21->column >> 8U, z13->column);
    EXPECT_EQ(z21->row >> 8U, z13->row);
}

TEST(TileMatrixSetTest, HorizontalWrappingAndRowsHaveExplicitRules) {
    TileMatrixSet matrix_set = TileMatrixSet::WebMercatorXYZ();

    EXPECT_EQ(matrix_set.NormalizeAddress(3, -1, 2),
              (ImageTileAddress{3, 7, 2}));
    EXPECT_FALSE(matrix_set.NormalizeAddress(3, 2, -1).has_value());
    EXPECT_FALSE(matrix_set.NormalizeAddress(3, 2, 8).has_value());

    matrix_set.wraps_horizontally = false;
    EXPECT_FALSE(matrix_set.NormalizeAddress(3, -1, 2).has_value());
}

TEST(TileMatrixSetTest, ParentIsIntegerOnly) {
    const ImageTileAddress child{21, 1307968, 792576};
    EXPECT_EQ(child.Parent(), (ImageTileAddress{20, 653984, 396288}));
    EXPECT_FALSE(ImageTileAddress{}.Parent().has_value());
}

TEST(ImageTileKeyTest, ProviderAndMatrixSetArePartOfIdentity) {
    const ImageTileKey osm = MakeKey(18, 163486, 99073);
    const ImageTileKey alternate_provider{"alternate", "WebMercatorQuad", osm.address};
    const ImageTileKey alternate_matrix{"osm", "CustomMatrix", osm.address};

    std::unordered_set<ImageTileKey, ImageTileKeyHash> keys;
    keys.insert(osm);
    keys.insert(alternate_provider);
    keys.insert(alternate_matrix);

    EXPECT_EQ(keys.size(), 3U);
}

TEST(TileProviderContractTest, BasicXyzProviderDeclaresSourceAndMatrixSet) {
    const BasicXYZTileProvider provider{
        "osm-main", "https://example.invalid/{z}/{x}/{y}.png", "", 2, 20};

    EXPECT_EQ(provider.GetImagerySourceId(), "osm-main");
    const TileMatrixSet matrix_set = provider.GetTileMatrixSet();
    EXPECT_TRUE(matrix_set.IsValid());
    EXPECT_EQ(matrix_set.id, "WebMercatorQuad");
    EXPECT_EQ(matrix_set.minimum_level, 2U);
    EXPECT_EQ(matrix_set.maximum_level, 20U);

    const auto key = provider.ResolveImageTileKey(TileCoordinates{3, 5, 4});
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->imagery_source_id, "osm-main");
    EXPECT_EQ(key->matrix_set_id, "WebMercatorQuad");
    EXPECT_EQ(key->address, (ImageTileAddress{4, 3, 5}));
    EXPECT_FALSE(provider.ResolveImageTileKey(TileCoordinates{0, 0, 1}).has_value());
}

}  // namespace earth_map::imagery
