#include <gtest/gtest.h>

#include <earth_map/imagery/tile_matrix_set.h>

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

TEST(PageTableWindowTest, ResolvesOnlyMatchingGenerationSourceAndLocalIntegerAddress) {
    const PageTableWindow window{
        kVirtualImageryAddressContractVersion,
        7,
        "osm",
        "WebMercatorQuad",
        21,
        1307900,
        792500,
        512,
        512,
    };
    const ImageTileKey key = MakeKey(21, 1307968, 792576);

    ASSERT_TRUE(window.IsValid());
    EXPECT_EQ(window.TryGetTexel(key), (PageTableTexel{68, 76}));
    EXPECT_FALSE(window.TryGetTexel(MakeKey(18, 163486, 99073)).has_value());
    EXPECT_FALSE(window.TryGetTexel(
        ImageTileKey{"alternate", "WebMercatorQuad", key.address}).has_value());
    EXPECT_FALSE(window.TryGetTexel(MakeKey(21, 1309000, 792576)).has_value());
}

TEST(PageTableWindowTest, RejectsUnknownContractVersion) {
    PageTableWindow window{
        kVirtualImageryAddressContractVersion + 1,
        1,
        "osm",
        "WebMercatorQuad",
        18,
        0,
        0,
        512,
        512,
    };

    EXPECT_FALSE(window.IsValid());
    EXPECT_FALSE(window.TryGetTexel(MakeKey(18, 0, 0)).has_value());
}

}  // namespace earth_map::imagery
