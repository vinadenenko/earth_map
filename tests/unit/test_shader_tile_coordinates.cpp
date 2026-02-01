/**
 * @file test_shader_tile_coordinates.cpp
 * @brief Tests that C++ tile coordinate calculations match the GLSL shader formulas
 *
 * The fragment shader computes tile coordinates from world position using
 * Web Mercator. This test verifies the C++ reference implementation
 * (CoordinateMapper::GeographicToSphericalTile) produces identical results.
 */

#include <gtest/gtest.h>
#include <earth_map/coordinates/coordinate_mapper.h>
#include <earth_map/math/tile_mathematics.h>
#include <cmath>

using namespace earth_map;
using namespace earth_map::coordinates;

namespace {

/**
 * @brief C++ reference implementation of the GLSL geoToTile() function
 *
 * This must exactly match the shader code in tile_atlas.frag.
 */
struct ShaderGeoToTile {
    static std::pair<int, int> Calculate(double lon, double lat, int zoom) {
        const double PI = 3.14159265359;
        const int n = 1 << zoom;

        const double norm_lon = (lon + 180.0) / 360.0;

        const double lat_clamped = std::clamp(lat, -85.0511, 85.0511);
        const double lat_rad = lat_clamped * PI / 180.0;
        const double merc_y = std::log(std::tan(PI / 4.0 + lat_rad / 2.0));
        const double norm_lat = (1.0 - merc_y / PI) / 2.0;

        int tile_x = static_cast<int>(std::floor(norm_lon * n));
        int tile_y = static_cast<int>(std::floor(norm_lat * n));

        tile_x = std::clamp(tile_x, 0, n - 1);
        tile_y = std::clamp(tile_y, 0, n - 1);

        return {tile_x, tile_y};
    }
};

} // namespace

class ShaderTileCoordinateTest : public ::testing::Test {};

TEST_F(ShaderTileCoordinateTest, ShaderMatchesCppAtZoom2_AllTiles) {
    constexpr int zoom = 2;
    constexpr int n = 1 << zoom;  // 4

    // Test center of each tile at zoom 2 (4x4 grid)
    for (int expected_x = 0; expected_x < n; ++expected_x) {
        for (int expected_y = 0; expected_y < n; ++expected_y) {
            // Compute center longitude/latitude of this tile using inverse mapping
            const double lon = (expected_x + 0.5) / n * 360.0 - 180.0;

            // Inverse Web Mercator for latitude
            const double norm_lat = (expected_y + 0.5) / n;
            const double merc_y = (1.0 - 2.0 * norm_lat) * M_PI;
            const double lat = std::atan(std::sinh(merc_y)) * 180.0 / M_PI;

            // Shader formula
            auto [shader_x, shader_y] = ShaderGeoToTile::Calculate(lon, lat, zoom);
            EXPECT_EQ(shader_x, expected_x)
                << "lon=" << lon << " lat=" << lat;
            EXPECT_EQ(shader_y, expected_y)
                << "lon=" << lon << " lat=" << lat;

            // CoordinateMapper formula
            Geographic geo(lat, lon, 0.0);
            TileCoordinates cpp_tile = CoordinateMapper::GeographicToSphericalTile(geo, zoom);
            EXPECT_EQ(cpp_tile.x, expected_x)
                << "CoordinateMapper mismatch at lon=" << lon << " lat=" << lat;
            EXPECT_EQ(cpp_tile.y, expected_y)
                << "CoordinateMapper mismatch at lon=" << lon << " lat=" << lat;

            // Cross-check: shader == C++
            EXPECT_EQ(shader_x, cpp_tile.x)
                << "Shader vs C++ mismatch at lon=" << lon << " lat=" << lat;
            EXPECT_EQ(shader_y, cpp_tile.y)
                << "Shader vs C++ mismatch at lon=" << lon << " lat=" << lat;
        }
    }
}

TEST_F(ShaderTileCoordinateTest, ShaderMatchesCppAtZoom0) {
    // At zoom 0, there's only one tile (0,0)
    auto [x, y] = ShaderGeoToTile::Calculate(0.0, 0.0, 0);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);

    Geographic geo(0.0, 0.0, 0.0);
    TileCoordinates cpp = CoordinateMapper::GeographicToSphericalTile(geo, 0);
    EXPECT_EQ(cpp.x, 0);
    EXPECT_EQ(cpp.y, 0);
}

TEST_F(ShaderTileCoordinateTest, ShaderHandlesPolarRegions) {
    // Near north pole (should clamp to valid Mercator range)
    auto [x_n, y_n] = ShaderGeoToTile::Calculate(0.0, 89.0, 2);
    EXPECT_GE(y_n, 0);
    EXPECT_LT(y_n, 4);

    // Near south pole
    auto [x_s, y_s] = ShaderGeoToTile::Calculate(0.0, -89.0, 2);
    EXPECT_GE(y_s, 0);
    EXPECT_LT(y_s, 4);

    // South should have higher Y than north in Web Mercator
    EXPECT_GT(y_s, y_n);
}

TEST_F(ShaderTileCoordinateTest, ShaderHandlesDateLine) {
    // Just west of date line
    auto [x_west, y_west] = ShaderGeoToTile::Calculate(-179.0, 0.0, 2);
    EXPECT_EQ(x_west, 0);

    // Just east of date line
    auto [x_east, y_east] = ShaderGeoToTile::Calculate(179.0, 0.0, 2);
    EXPECT_EQ(x_east, 3);
}

TEST_F(ShaderTileCoordinateTest, ZoomPrecisionWithBitshift) {
    // This test verifies that 1 << zoom gives the correct result
    // (the bug was pow(2.0, zoom) returning 3.999... for zoom=2)
    for (int zoom = 0; zoom <= 10; ++zoom) {
        const int n_bitshift = 1 << zoom;
        const int n_pow = static_cast<int>(std::pow(2.0, zoom));
        EXPECT_EQ(n_bitshift, n_pow)
            << "Bitshift vs pow mismatch at zoom=" << zoom;
    }
}
