/**
 * @file test_shader_tile_coordinates.cpp
 * @brief Tests that C++ tile coordinate calculations match the GLSL shader formulas
 *
 * The fragment shader computes tile coordinates from a ray-hit point using
 * Web Mercator. These tests verify that its reference calculation, the
 * legacy renderer boundary, and the canonical TileMatrixSet agree.
 */

#include <gtest/gtest.h>
#include <earth_map/coordinates/coordinate_mapper.h>
#include <earth_map/imagery/tile_matrix_set.h>
#include <earth_map/math/tile_mathematics.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

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

        tile_x = tile_x == n ? 0 : std::clamp(tile_x, 0, n - 1);
        tile_y = std::clamp(tile_y, 0, n - 1);

        return {tile_x, tile_y};
    }
};

std::optional<imagery::ImageTileAddress> ShaderSurfaceToTile(
    glm::vec3 ray_hit,
    std::uint32_t zoom) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kMaxMercatorSinLatitude = 0.9962721f;

    // This is the CPU reference for surfaceToTileAndFrac in the fragment
    // shader. A ray/sphere hit is not assumed to be exactly unit length.
    const glm::vec3 point = glm::normalize(ray_hit);
    const float normalized_x = (std::atan2(point.x, point.z) / kPi + 1.0f) * 0.5f;
    const float sin_latitude = std::clamp(
        point.y, -kMaxMercatorSinLatitude, kMaxMercatorSinLatitude);
    const float normalized_y = (1.0f - 0.5f * std::log(
        (1.0f + sin_latitude) / (1.0f - sin_latitude)) / kPi) * 0.5f;
    const int dimension = 1 << zoom;

    const int raw_column =
        static_cast<int>(std::floor(normalized_x * static_cast<float>(dimension)));
    const int column = raw_column == dimension
        ? 0
        : std::clamp(raw_column, 0, dimension - 1);
    return imagery::ImageTileAddress{
        zoom,
        static_cast<std::uint32_t>(column),
        static_cast<std::uint32_t>(std::clamp(
            static_cast<int>(std::floor(normalized_y * static_cast<float>(dimension))),
            0, dimension - 1)),
    };
}

glm::vec3 UnitSpherePoint(double latitude_radians, double longitude_radians) {
    const float latitude = static_cast<float>(latitude_radians);
    const float longitude = static_cast<float>(longitude_radians);
    return {
        std::cos(latitude) * std::sin(longitude),
        std::sin(latitude),
        std::cos(latitude) * std::cos(longitude),
    };
}

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

    // The declared XYZ matrix wraps exactly at +180° rather than creating a
    // second copy of the easternmost tile column.
    auto [x_wrap, y_wrap] = ShaderGeoToTile::Calculate(180.0, 0.0, 2);
    EXPECT_EQ(x_wrap, 0);
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

TEST_F(ShaderTileCoordinateTest, RayHitAndCanonicalMatrixAgreeAtHighZoom) {
    const imagery::TileMatrixSet matrix_set = imagery::TileMatrixSet::WebMercatorXYZ();
    const geodesy::GeodeticPosition yerevan{
        40.1872 * std::numbers::pi_v<double> / 180.0,
        44.5152 * std::numbers::pi_v<double> / 180.0,
        0.0,
    };
    const glm::vec3 ray_hit = UnitSpherePoint(
        yerevan.latitude_radians, yerevan.longitude_radians) * 1.0001f;

    for (const std::uint32_t zoom : {13U, 18U, 21U}) {
        const auto canonical = matrix_set.GeodeticToTile(yerevan, zoom);
        const auto shader = ShaderSurfaceToTile(ray_hit, zoom);
        ASSERT_TRUE(canonical.has_value());
        ASSERT_TRUE(shader.has_value());

        EXPECT_EQ(*shader, *canonical) << "zoom=" << zoom;

        const Geographic legacy_geographic{
            yerevan.latitude_radians * 180.0 / std::numbers::pi_v<double>,
            yerevan.longitude_radians * 180.0 / std::numbers::pi_v<double>,
            0.0,
        };
        const TileCoordinates legacy_tile =
            CoordinateMapper::GeographicToSphericalTile(
                legacy_geographic, static_cast<std::int32_t>(zoom));
        EXPECT_EQ(legacy_tile.x, static_cast<std::int32_t>(canonical->column));
        EXPECT_EQ(legacy_tile.y, static_cast<std::int32_t>(canonical->row));
        EXPECT_EQ(legacy_tile.zoom, static_cast<std::int32_t>(canonical->level));
    }
}
