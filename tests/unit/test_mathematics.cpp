#include <gtest/gtest.h>
#include "earth_map/math/coordinate_system.h"
#include "earth_map/math/projection.h"
#include "earth_map/math/geodetic_calculations.h"
#include "earth_map/math/tile_mathematics.h"
#include <cmath>
#include <limits>

using namespace earth_map;

class CoordinateSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test locations
        greenwich_ = GeographicCoordinates(51.4778, -0.0015, 0.0);  // Greenwich Observatory
        san_francisco_ = GeographicCoordinates(37.7749, -122.4194, 100.0);  // San Francisco
        north_pole_ = GeographicCoordinates(90.0, 0.0, 0.0);  // North Pole
        south_pole_ = GeographicCoordinates(-90.0, 0.0, 0.0);  // South Pole
        equator_prime_meridian_ = GeographicCoordinates(0.0, 0.0, 0.0);  // Equator at Prime Meridian
    }
    
    GeographicCoordinates greenwich_;
    GeographicCoordinates san_francisco_;
    GeographicCoordinates north_pole_;
    GeographicCoordinates south_pole_;
    GeographicCoordinates equator_prime_meridian_;
};

TEST_F(CoordinateSystemTest, GeographicCoordinatesValidation) {
    // Valid coordinates
    EXPECT_TRUE(greenwich_.IsValid());
    EXPECT_TRUE(san_francisco_.IsValid());
    EXPECT_TRUE(north_pole_.IsValid());
    EXPECT_TRUE(south_pole_.IsValid());
    EXPECT_TRUE(equator_prime_meridian_.IsValid());
    
    // Invalid coordinates
    GeographicCoordinates invalid_lat(91.0, 0.0, 0.0);  // Latitude too high
    EXPECT_FALSE(invalid_lat.IsValid());
    
    GeographicCoordinates invalid_lon(0.0, 181.0, 0.0);  // Longitude too high
    EXPECT_FALSE(invalid_lon.IsValid());
    
    GeographicCoordinates invalid_lat_neg(-91.0, 0.0, 0.0);  // Latitude too low
    EXPECT_FALSE(invalid_lat_neg.IsValid());
    
    GeographicCoordinates invalid_lon_neg(0.0, -181.0, 0.0);  // Longitude too low
    EXPECT_FALSE(invalid_lon_neg.IsValid());
}

TEST_F(CoordinateSystemTest, LongitudeNormalization) {
    GeographicCoordinates test_coord(0.0, 190.0, 0.0);
    test_coord.NormalizeLongitude();
    EXPECT_NEAR(test_coord.longitude, -170.0, 1e-10);
    
    test_coord.longitude = -190.0;
    test_coord.NormalizeLongitude();
    EXPECT_NEAR(test_coord.longitude, 170.0, 1e-10);
    
    test_coord.longitude = 360.0;
    test_coord.NormalizeLongitude();
    EXPECT_NEAR(test_coord.longitude, 0.0, 1e-10);
}

TEST_F(CoordinateSystemTest, RadianConversion) {
    EXPECT_NEAR(greenwich_.LatitudeRadians(), 0.898973719, 1e-9);
    EXPECT_NEAR(greenwich_.LongitudeRadians(), -0.000026179, 1e-9);
    
    EXPECT_NEAR(san_francisco_.LatitudeRadians(), 0.659237, 1e-6);
    EXPECT_NEAR(san_francisco_.LongitudeRadians(), -2.13544, 1e-6);
}

TEST_F(CoordinateSystemTest, GeographicToECEF) {
    const glm::dvec3 greenwich_ecef = CoordinateSystem::GeographicToECEF(greenwich_);
    const double expected_radius = WGS84Ellipsoid::SEMI_MAJOR_AXIS;
    const double actual_radius = glm::length(greenwich_ecef);
    
    EXPECT_NEAR(actual_radius, expected_radius, 100.0);  // Within 100m
    EXPECT_GT(greenwich_ecef.x, 0);  // Should be in eastern hemisphere
    EXPECT_NEAR(greenwich_ecef.z, expected_radius * std::sin(greenwich_.LatitudeRadians()), 100.0);
}

TEST_F(CoordinateSystemTest, ECEFTGeographicRoundTrip) {
    const glm::dvec3 original_ecef = CoordinateSystem::GeographicToECEF(san_francisco_);
    const GeographicCoordinates recovered_geo = CoordinateSystem::ECEFToGeographic(original_ecef);
    
    EXPECT_NEAR(recovered_geo.latitude, san_francisco_.latitude, 1e-7);
    EXPECT_NEAR(recovered_geo.longitude, san_francisco_.longitude, 1e-7);
    EXPECT_NEAR(recovered_geo.altitude, san_francisco_.altitude, 1.0);
}

TEST_F(CoordinateSystemTest, ENUTransformations) {
    const GeographicCoordinates origin(0.0, 0.0, 0.0);
    const GeographicCoordinates target(1.0, 1.0, 0.0);
    
    const glm::dvec3 enu_coords = CoordinateSystem::GeographicToENU(target, origin);
    const GeographicCoordinates recovered = CoordinateSystem::ENUToGeographic(enu_coords, origin);
    
    EXPECT_NEAR(recovered.latitude, target.latitude, 1e-6);
    EXPECT_NEAR(recovered.longitude, target.longitude, 1e-6);
    EXPECT_NEAR(recovered.altitude, target.altitude, 1.0);
}

TEST_F(CoordinateSystemTest, SurfaceNormal) {
    const glm::dvec3 normal_greenwich = CoordinateSystem::SurfaceNormal(greenwich_);
    const double expected_z = std::sin(greenwich_.LatitudeRadians());
    
    EXPECT_NEAR(glm::length(normal_greenwich), 1.0, 1e-10);  // Should be unit vector
    EXPECT_NEAR(normal_greenwich.z, expected_z, 1e-3);
}

class ProjectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        web_mercator_ = std::static_pointer_cast<WebMercatorProjection>(
            ProjectionRegistry::GetProjection(ProjectionType::WEB_MERCATOR)
        );
        wgs84_ = std::static_pointer_cast<WGS84Projection>(
            ProjectionRegistry::GetProjection(ProjectionType::WGS84)
        );
        equirectangular_ = std::static_pointer_cast<EquirectangularProjection>(
            ProjectionRegistry::GetProjection(ProjectionType::EQUIRECTANGULAR)
        );
    }
    
    std::shared_ptr<WebMercatorProjection> web_mercator_;
    std::shared_ptr<WGS84Projection> wgs84_;
    std::shared_ptr<EquirectangularProjection> equirectangular_;
};

TEST_F(ProjectionTest, WebMercatorProjection) {
    const GeographicCoordinates greenwich(51.4778, -0.0015, 0.0);
    const ProjectedCoordinates proj = web_mercator_->Project(greenwich);
    
    // Greenwich should be near x=0 in Web Mercator
    EXPECT_NEAR(proj.x, 0.0, 1000.0);  // Within 1km
    
    // Test round-trip
    const GeographicCoordinates recovered = web_mercator_->Unproject(proj);
    EXPECT_NEAR(recovered.latitude, greenwich.latitude, 1e-6);
    EXPECT_NEAR(recovered.longitude, greenwich.longitude, 1e-6);
}

TEST_F(ProjectionTest, WebMercatorBounds) {
    EXPECT_FALSE(web_mercator_->IsValidLocation(GeographicCoordinates(85.1, 0.0, 0.0)));
    EXPECT_FALSE(web_mercator_->IsValidLocation(GeographicCoordinates(-85.1, 0.0, 0.0)));
    EXPECT_TRUE(web_mercator_->IsValidLocation(GeographicCoordinates(85.0, 0.0, 0.0)));
    EXPECT_TRUE(web_mercator_->IsValidLocation(GeographicCoordinates(-85.0, 0.0, 0.0)));
}

TEST_F(ProjectionTest, WGS84Projection) {
    const GeographicCoordinates original(40.7128, -74.0060, 0.0);  // New York
    const ProjectedCoordinates proj = wgs84_->Project(original);
    
    // WGS84 should be identity projection
    EXPECT_NEAR(proj.x, original.longitude, 1e-10);
    EXPECT_NEAR(proj.y, original.latitude, 1e-10);
    
    const GeographicCoordinates recovered = wgs84_->Unproject(proj);
    EXPECT_NEAR(recovered.latitude, original.latitude, 1e-10);
    EXPECT_NEAR(recovered.longitude, original.longitude, 1e-10);
}

TEST_F(ProjectionTest, ProjectionRegistry) {
    // Test getting projections by different methods
    const auto proj_by_type = ProjectionRegistry::GetProjection(ProjectionType::WEB_MERCATOR);
    const auto proj_by_epsg = ProjectionRegistry::GetProjection(3857);
    const auto proj_by_name = ProjectionRegistry::GetProjection("Web Mercator");
    
    EXPECT_NE(proj_by_type, nullptr);
    EXPECT_NE(proj_by_epsg, nullptr);
    EXPECT_NE(proj_by_name, nullptr);
    
    EXPECT_EQ(proj_by_type->GetType(), ProjectionType::WEB_MERCATOR);
    EXPECT_EQ(proj_by_epsg->GetEPSGCode(), 3857);
    EXPECT_EQ(proj_by_name->GetName(), "Web Mercator");
}

class GeodeticCalculatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        new_york_ = GeographicCoordinates(40.7128, -74.0060, 0.0);
        los_angeles_ = GeographicCoordinates(34.0522, -118.2437, 0.0);
        london_ = GeographicCoordinates(51.5074, -0.1278, 0.0);
        paris_ = GeographicCoordinates(48.8566, 2.3522, 0.0);
    }
    
    GeographicCoordinates new_york_;
    GeographicCoordinates los_angeles_;
    GeographicCoordinates london_;
    GeographicCoordinates paris_;
};

TEST_F(GeodeticCalculatorTest, HaversineDistance) {
    // Test known distance: New York to Los Angeles (~3944 km)
    const double distance = GeodeticCalculator::HaversineDistance(new_york_, los_angeles_);
    EXPECT_NEAR(distance, 3944000.0, 50000.0);  // Within 50km
    
    // Test known distance: London to Paris (~344 km)
    const double distance_europe = GeodeticCalculator::HaversineDistance(london_, paris_);
    EXPECT_NEAR(distance_europe, 344000.0, 10000.0);  // Within 10km
}

TEST_F(GeodeticCalculatorTest, VincentyDistance) {
    const double vincenty_distance = GeodeticCalculator::VincentyDistance(new_york_, los_angeles_);
    const double haversine_distance = GeodeticCalculator::HaversineDistance(new_york_, los_angeles_);
    
    // Vincenty should be slightly more accurate, but should be close to Haversine
    EXPECT_NEAR(vincenty_distance, haversine_distance, 1000.0);  // Within 1km
}

TEST_F(GeodeticCalculatorTest, DistanceAndBearing) {
    const DistanceResult result = GeodeticCalculator::HaversineDistanceAndBearing(london_, paris_);
    
    EXPECT_GT(result.distance, 0);
    EXPECT_GE(result.initial_bearing, 0);
    EXPECT_LT(result.initial_bearing, 360);
    EXPECT_GE(result.final_bearing, 0);
    EXPECT_LT(result.final_bearing, 360);
}

TEST_F(GeodeticCalculatorTest, InitialBearing) {
    const double bearing = GeodeticCalculator::InitialBearing(london_, paris_);
    // London to Paris should be approximately southeast (around 135 degrees)
    EXPECT_GT(bearing, 100);
    EXPECT_LT(bearing, 170);
}

TEST_F(GeodeticCalculatorTest, DestinationPoint) {
    const GeographicCoordinates start = london_;
    const double bearing = 45.0;  // Northeast
    const double distance = 100000.0;  // 100 km
    
    const GeographicCoordinates destination = GeodeticCalculator::DestinationPoint(start, bearing, distance);
    
    // Destination should be northeast of start
    EXPECT_GT(destination.latitude, start.latitude);
    EXPECT_GT(destination.longitude, start.longitude);
    
    // Distance back should be approximately the same
    const double distance_back = GeodeticCalculator::HaversineDistance(start, destination);
    EXPECT_NEAR(distance_back, distance, 1000.0);  // Within 1km
}

TEST_F(GeodeticCalculatorTest, CrossTrackDistance) {
    const GeographicCoordinates path_start(0.0, 0.0, 0.0);
    const GeographicCoordinates path_end(0.0, 1.0, 0.0);  // Due east
    const GeographicCoordinates point(0.1, 0.5, 0.0);  // North of path
    
    const double cross_track = GeodeticCalculator::CrossTrackDistance(point, path_start, path_end);
    EXPECT_GT(cross_track, 0);
    
    // Should be approximately 11.1 km (0.1 degree latitude)
    EXPECT_NEAR(cross_track, 11100.0, 1000.0);
}

class GeodeticPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_points_ = {
            GeographicCoordinates(0.0, 0.0, 0.0),
            GeographicCoordinates(0.0, 1.0, 0.0),
            GeographicCoordinates(1.0, 1.0, 0.0),
            GeographicCoordinates(1.0, 0.0, 0.0)
        };
    }
    
    std::vector<GeographicCoordinates> path_points_;
};

TEST_F(GeodeticPathTest, CalculateLength) {
    const double length = GeodeticPath::CalculateLength(path_points_);
    EXPECT_GT(length, 0);
    
    // Should be approximately 3 segments of ~111km plus one diagonal
    EXPECT_GT(length, 300000.0);  // At least 300km
    EXPECT_LT(length, 500000.0);  // Less than 500km
}

TEST_F(GeodeticPathTest, CalculateCentroid) {
    const GeographicCoordinates centroid = GeodeticPath::CalculateCentroid(path_points_);
    
    // Should be near (0.5, 0.5)
    EXPECT_NEAR(centroid.latitude, 0.5, 0.1);
    EXPECT_NEAR(centroid.longitude, 0.5, 0.1);
}

TEST_F(GeodeticPathTest, PointInPolygon) {
    const GeographicCoordinates inside(0.5, 0.5, 0.0);
    const GeographicCoordinates outside(2.0, 2.0, 0.0);
    const GeographicCoordinates on_edge(0.0, 0.5, 0.0);
    
    EXPECT_TRUE(GeodeticPath::PointInPolygon(inside, path_points_));
    EXPECT_FALSE(GeodeticPath::PointInPolygon(outside, path_points_));
}

class TileMathematicsTest : public ::testing::Test {
protected:
    void SetUp() override {
        greenwich_tile_ = TileCoordinates(0, 0, 0);  // Greenwich at zoom 0
        san_francisco_tile_ = TileCoordinates(326, 791, 10);  // San Francisco at zoom 10
    }
    
    TileCoordinates greenwich_tile_;
    TileCoordinates san_francisco_tile_;
};

TEST_F(TileMathematicsTest, TileCoordinatesValidation) {
    EXPECT_TRUE(greenwich_tile_.IsValid());
    EXPECT_TRUE(san_francisco_tile_.IsValid());
    
    // Invalid tiles
    EXPECT_FALSE(TileCoordinates(-1, 0, 0).IsValid());
    EXPECT_FALSE(TileCoordinates(0, -1, 0).IsValid());
    EXPECT_FALSE(TileCoordinates(1, 0, 0).IsValid());  // x >= 2^zoom
    EXPECT_FALSE(TileCoordinates(0, 1, 0).IsValid());  // y >= 2^zoom
    EXPECT_FALSE(TileCoordinates(0, 0, -1).IsValid());  // negative zoom
    EXPECT_FALSE(TileCoordinates(0, 0, 31).IsValid());  // zoom too high
}

TEST_F(TileMathematicsTest, TileHierarchy) {
    const TileCoordinates child1 = san_francisco_tile_.GetChildren()[0];
    EXPECT_EQ(child1.zoom, san_francisco_tile_.zoom + 1);
    EXPECT_EQ(child1.x, san_francisco_tile_.x * 2);
    EXPECT_EQ(child1.y, san_francisco_tile_.y * 2);
    
    const TileCoordinates parent = san_francisco_tile_.GetParent();
    EXPECT_EQ(parent.zoom, san_francisco_tile_.zoom - 1);
    EXPECT_EQ(parent.x, san_francisco_tile_.x / 2);
    EXPECT_EQ(parent.y, san_francisco_tile_.y / 2);
}

TEST_F(TileMathematicsTest, GeographicToTileRoundTrip) {
    const GeographicCoordinates san_francisco(37.7749, -122.4194, 0.0);
    const TileCoordinates tile = TileMathematics::GeographicToTile(san_francisco, 10);
    const GeographicCoordinates recovered = TileMathematics::TileToGeographic(tile);
    
    EXPECT_EQ(tile.zoom, 10);
    EXPECT_NEAR(recovered.latitude, san_francisco.latitude, 0.1);  // Within tile size
    EXPECT_NEAR(recovered.longitude, san_francisco.longitude, 0.1);
}

TEST_F(TileMathematicsTest, TileBounds) {
    const BoundingBox2D bounds = TileMathematics::GetTileBounds(san_francisco_tile_);
    EXPECT_TRUE(bounds.IsValid());
    
    // San Francisco should be within these bounds
    const GeographicCoordinates sf_center = TileMathematics::TileToGeographic(san_francisco_tile_);
    EXPECT_TRUE(bounds.Contains(glm::dvec2(sf_center.longitude, sf_center.latitude)));
}

TEST_F(TileMathematicsTest, TileDistance) {
    const TileCoordinates tile1(0, 0, 10);
    const TileCoordinates tile2(3, 4, 10);
    
    const double distance = TileMathematics::TileDistance(tile1, tile2);
    EXPECT_NEAR(distance, 5.0, 1e-10);  // 3-4-5 triangle
}

TEST_F(TileMathematicsTest, AdjacentTiles) {
    const TileCoordinates center(5, 5, 10);
    const std::vector<TileCoordinates> neighbors = TileMathematics::GetNeighborTiles(center, false);
    
    EXPECT_EQ(neighbors.size(), 4);  // Cardinal directions only
    
    // Check cardinal directions
    bool has_north = false, has_south = false, has_east = false, has_west = false;
    for (const auto& neighbor : neighbors) {
        if (neighbor.x == 5 && neighbor.y == 4) has_north = true;
        if (neighbor.x == 5 && neighbor.y == 6) has_south = true;
        if (neighbor.x == 6 && neighbor.y == 5) has_east = true;
        if (neighbor.x == 4 && neighbor.y == 5) has_west = true;
    }
    
    EXPECT_TRUE(has_north);
    EXPECT_TRUE(has_south);
    EXPECT_TRUE(has_east);
    EXPECT_TRUE(has_west);
}

TEST_F(TileMathematicsTest, QuadtreeKey) {
    const QuadtreeKey quadkey(san_francisco_tile_);
    EXPECT_TRUE(quadkey.IsValid());
    
    const TileCoordinates recovered = quadkey.ToTileCoordinates();
    EXPECT_EQ(recovered, san_francisco_tile_);
    
    const QuadtreeKey parent = quadkey.GetParent();
    EXPECT_EQ(parent.ToTileCoordinates(), san_francisco_tile_.GetParent());
}

TEST_F(TileMathematicsTest, GroundResolution) {
    const double resolution = TileMathematics::CalculateGroundResolution(10, 37.7749);
    EXPECT_GT(resolution, 0);
    EXPECT_LT(resolution, 1000);  // Should be less than 1km per pixel at zoom 10
    
    // Resolution should decrease with higher zoom
    const double resolution_higher = TileMathematics::CalculateGroundResolution(12, 37.7749);
    EXPECT_LT(resolution_higher, resolution);
}

class TerrainCalculatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        low_point_ = GeographicCoordinates(0.0, 0.0, 100.0);
        high_point_ = GeographicCoordinates(0.001, 0.0, 200.0);  // ~111m north, 100m higher
    }
    
    GeographicCoordinates low_point_;
    GeographicCoordinates high_point_;
};

TEST_F(TerrainCalculatorTest, CalculateSlope) {
    const double slope = TerrainCalculator::CalculateSlope(low_point_, high_point_);
    EXPECT_GT(slope, 0);
    EXPECT_LT(slope, 90);  // Should be less than vertical
    
    // Should be approximately atan(100m / 111m) = atan(0.9) ≈ 42 degrees
    EXPECT_NEAR(slope, 42.0, 5.0);
}

TEST_F(TerrainCalculatorTest, LineOfSight) {
    const GeographicCoordinates observer(low_point_);
    const GeographicCoordinates target(high_point_);
    
    // Should have line of sight since target is higher
    EXPECT_TRUE(TerrainCalculator::LineOfSight(observer, target));
}