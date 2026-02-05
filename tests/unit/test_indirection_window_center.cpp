/**
 * @file test_indirection_window_center.cpp
 * @brief Tests for indirection texture window centering
 *
 * Verifies that the window center for the indirection texture accounts for
 * Web Mercator nonlinearity. The Mercator Y mapping compresses tiles near
 * the equator and stretches them near the poles, so a geographically
 * symmetric viewport maps to an asymmetric tile-Y range. The window
 * center must use the midpoint of the actual tile range, not the nadir.
 */

#include <gtest/gtest.h>
#include <earth_map/coordinates/coordinate_mapper.h>
#include <earth_map/coordinates/coordinate_spaces.h>
#include <cmath>

using namespace earth_map;
using namespace earth_map::coordinates;

class IndirectionWindowCenterTest : public ::testing::Test {};

TEST_F(IndirectionWindowCenterTest, MercatorAsymmetryExistsAtHighLatitude) {
    // At 40N, symmetric geographic bounds produce asymmetric tile-Y ranges.
    // The poleward (north) side spans more tile-Y units than the equatorward
    // (south) side because Mercator stretching increases toward the poles.
    const int zoom = 17;

    Geographic cam_geo(40.0, 44.0, 0.0);
    TileCoordinates nadir =
        CoordinateMapper::GeographicToSphericalTile(cam_geo, zoom);

    // Symmetric bounds: +-0.3 degrees around 40N
    Geographic min_geo(39.7, 43.7, 0.0);
    Geographic max_geo(40.3, 44.3, 0.0);
    TileCoordinates min_tile =
        CoordinateMapper::GeographicToSphericalTile(min_geo, zoom);
    TileCoordinates max_tile =
        CoordinateMapper::GeographicToSphericalTile(max_geo, zoom);

    int32_t bounds_center_y = (min_tile.y + max_tile.y) / 2;

    // Bounds center in tile-Y should differ from nadir tile-Y
    EXPECT_NE(bounds_center_y, nadir.y);

    // The bounds center should be shifted poleward (lower tile-Y = more north)
    // compared to nadir, because the poleward side spans more tiles.
    EXPECT_LT(bounds_center_y, nadir.y);
}

TEST_F(IndirectionWindowCenterTest, MercatorAsymmetryFlipsInSouthernHemisphere) {
    const int zoom = 17;

    Geographic cam_geo(-40.0, 44.0, 0.0);
    TileCoordinates nadir =
        CoordinateMapper::GeographicToSphericalTile(cam_geo, zoom);

    Geographic min_geo(-40.3, 43.7, 0.0);
    Geographic max_geo(-39.7, 44.3, 0.0);
    TileCoordinates min_tile =
        CoordinateMapper::GeographicToSphericalTile(min_geo, zoom);
    TileCoordinates max_tile =
        CoordinateMapper::GeographicToSphericalTile(max_geo, zoom);

    int32_t bounds_center_y = (min_tile.y + max_tile.y) / 2;

    // In southern hemisphere, the bounds center should shift poleward
    // (higher tile-Y = more south)
    EXPECT_GT(bounds_center_y, nadir.y);
}

TEST_F(IndirectionWindowCenterTest, NoAsymmetryAtEquator) {
    const int zoom = 17;

    Geographic cam_geo(0.0, 44.0, 0.0);
    TileCoordinates nadir =
        CoordinateMapper::GeographicToSphericalTile(cam_geo, zoom);

    // Symmetric bounds around equator
    Geographic min_geo(-0.3, 43.7, 0.0);
    Geographic max_geo(0.3, 44.3, 0.0);
    TileCoordinates min_tile =
        CoordinateMapper::GeographicToSphericalTile(min_geo, zoom);
    TileCoordinates max_tile =
        CoordinateMapper::GeographicToSphericalTile(max_geo, zoom);

    int32_t bounds_center_y = (min_tile.y + max_tile.y) / 2;

    // At the equator, nadir and bounds center should be very close
    EXPECT_NEAR(bounds_center_y, nadir.y, 1);
}

TEST_F(IndirectionWindowCenterTest, AsymmetryGrowsWithZoomLevel) {
    // The tile-count offset between nadir and bounds center should grow
    // with zoom level because the same angular span maps to more tiles.
    Geographic cam_geo(50.0, 10.0, 0.0);
    Geographic min_geo(49.7, 9.7, 0.0);
    Geographic max_geo(50.3, 10.3, 0.0);

    auto compute_offset = [&](int zoom) -> int32_t {
        TileCoordinates nadir =
            CoordinateMapper::GeographicToSphericalTile(cam_geo, zoom);
        TileCoordinates min_tile =
            CoordinateMapper::GeographicToSphericalTile(min_geo, zoom);
        TileCoordinates max_tile =
            CoordinateMapper::GeographicToSphericalTile(max_geo, zoom);
        int32_t bounds_center_y = (min_tile.y + max_tile.y) / 2;
        return std::abs(bounds_center_y - nadir.y);
    };

    int32_t offset_z15 = compute_offset(15);
    int32_t offset_z17 = compute_offset(17);
    int32_t offset_z19 = compute_offset(19);

    EXPECT_LT(offset_z15, offset_z17);
    EXPECT_LT(offset_z17, offset_z19);
}
