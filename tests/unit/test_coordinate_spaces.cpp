/**
 * @file test_coordinate_spaces.cpp
 * @brief Unit tests for type-safe coordinate space definitions
 *
 * Following TDD methodology: Tests written before implementation
 */

#include <gtest/gtest.h>
#include <earth_map/coordinates/coordinate_spaces.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace earth_map::coordinates;

// ============================================================================
// Geographic Tests
// ============================================================================

class GeographicTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(GeographicTest, DefaultConstructor_CreatesInvalidCoordinates) {
    Geographic geo;
    EXPECT_FALSE(geo.IsValid());
    EXPECT_TRUE(std::isnan(geo.latitude));
    EXPECT_TRUE(std::isnan(geo.longitude));
}

TEST_F(GeographicTest, ValidCoordinates_PassValidation) {
    Geographic nyc(40.7128, -74.0060, 0.0);
    EXPECT_TRUE(nyc.IsValid());
    EXPECT_DOUBLE_EQ(40.7128, nyc.latitude);
    EXPECT_DOUBLE_EQ(-74.0060, nyc.longitude);
    EXPECT_DOUBLE_EQ(0.0, nyc.altitude);
}

TEST_F(GeographicTest, InvalidLatitude_FailsValidation) {
    Geographic invalid_north(91.0, 0.0, 0.0);
    EXPECT_FALSE(invalid_north.IsValid());

    Geographic invalid_south(-91.0, 0.0, 0.0);
    EXPECT_FALSE(invalid_south.IsValid());
}

TEST_F(GeographicTest, InvalidLongitude_FailsValidation) {
    Geographic invalid_east(0.0, 181.0, 0.0);
    EXPECT_FALSE(invalid_east.IsValid());

    Geographic invalid_west(0.0, -181.0, 0.0);
    EXPECT_FALSE(invalid_west.IsValid());
}

TEST_F(GeographicTest, BoundaryCoordinates_AreValid) {
    Geographic north_pole(90.0, 0.0, 0.0);
    EXPECT_TRUE(north_pole.IsValid());

    Geographic south_pole(-90.0, 0.0, 0.0);
    EXPECT_TRUE(south_pole.IsValid());

    Geographic dateline_west(0.0, -180.0, 0.0);
    EXPECT_TRUE(dateline_west.IsValid());

    Geographic dateline_east(0.0, 180.0, 0.0);
    EXPECT_TRUE(dateline_east.IsValid());
}

TEST_F(GeographicTest, Normalized_WrapsLongitudeCorrectly) {
    Geographic over_east(0.0, 190.0, 0.0);
    Geographic normalized = over_east.Normalized();
    EXPECT_NEAR(-170.0, normalized.longitude, 1e-9);

    Geographic over_west(0.0, -190.0, 0.0);
    normalized = over_west.Normalized();
    EXPECT_NEAR(170.0, normalized.longitude, 1e-9);

    Geographic full_wrap(0.0, 370.0, 0.0);
    normalized = full_wrap.Normalized();
    EXPECT_NEAR(10.0, normalized.longitude, 1e-9);
}

TEST_F(GeographicTest, IsApproximatelyEqual_DetectsSimilarPoints) {
    Geographic p1(40.7128, -74.0060, 0.0);
    Geographic p2(40.7128, -74.0060, 0.0);
    EXPECT_TRUE(p1.IsApproximatelyEqual(p2));

    Geographic p3(40.7129, -74.0061, 0.0);  // 0.0001 degree difference
    EXPECT_TRUE(p1.IsApproximatelyEqual(p3, 0.001));
    EXPECT_FALSE(p1.IsApproximatelyEqual(p3, 0.00001));
}

TEST_F(GeographicTest, EqualityOperators_WorkCorrectly) {
    Geographic p1(40.7128, -74.0060, 0.0);
    Geographic p2(40.7128, -74.0060, 0.0);
    Geographic p3(40.7129, -74.0060, 0.0);

    EXPECT_TRUE(p1 == p2);
    EXPECT_FALSE(p1 == p3);
    EXPECT_FALSE(p1 != p2);
    EXPECT_TRUE(p1 != p3);
}

// ============================================================================
// GeographicBounds Tests
// ============================================================================

class GeographicBoundsTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(GeographicBoundsTest, DefaultConstructor_CreatesInvalidBounds) {
    GeographicBounds bounds;
    EXPECT_FALSE(bounds.IsValid());
}

TEST_F(GeographicBoundsTest, ValidBounds_PassValidation) {
    Geographic sw(40.0, -75.0, 0.0);
    Geographic ne(41.0, -74.0, 0.0);
    GeographicBounds bounds(sw, ne);

    EXPECT_TRUE(bounds.IsValid());
}

TEST_F(GeographicBoundsTest, InvertedBounds_FailValidation) {
    Geographic sw(41.0, -74.0, 0.0);  // Latitude too high for SW corner
    Geographic ne(40.0, -75.0, 0.0);
    GeographicBounds bounds(sw, ne);

    EXPECT_FALSE(bounds.IsValid());
}

TEST_F(GeographicBoundsTest, Contains_DetectsPointInside) {
    Geographic sw(40.0, -75.0, 0.0);
    Geographic ne(41.0, -74.0, 0.0);
    GeographicBounds bounds(sw, ne);

    Geographic inside(40.5, -74.5, 0.0);
    EXPECT_TRUE(bounds.Contains(inside));

    Geographic outside(42.0, -74.5, 0.0);
    EXPECT_FALSE(bounds.Contains(outside));
}

TEST_F(GeographicBoundsTest, GetCenter_ReturnsCorrectMidpoint) {
    Geographic sw(40.0, -75.0, 0.0);
    Geographic ne(42.0, -73.0, 0.0);
    GeographicBounds bounds(sw, ne);

    Geographic center = bounds.GetCenter();
    EXPECT_DOUBLE_EQ(41.0, center.latitude);
    EXPECT_DOUBLE_EQ(-74.0, center.longitude);
}

TEST_F(GeographicBoundsTest, WidthHeight_ReturnCorrectDimensions) {
    Geographic sw(40.0, -75.0, 0.0);
    Geographic ne(42.0, -73.0, 0.0);
    GeographicBounds bounds(sw, ne);

    EXPECT_DOUBLE_EQ(2.0, bounds.Width());   // 2 degrees longitude
    EXPECT_DOUBLE_EQ(2.0, bounds.Height());  // 2 degrees latitude
}

TEST_F(GeographicBoundsTest, ExpandToInclude_GrowsBounds) {
    Geographic sw(40.0, -75.0, 0.0);
    Geographic ne(41.0, -74.0, 0.0);
    GeographicBounds bounds(sw, ne);

    Geographic outside(42.0, -76.0, 0.0);
    bounds.ExpandToInclude(outside);

    EXPECT_DOUBLE_EQ(42.0, bounds.max.latitude);
    EXPECT_DOUBLE_EQ(-76.0, bounds.min.longitude);
}

TEST_F(GeographicBoundsTest, Intersects_DetectsOverlap) {
    Geographic sw1(40.0, -75.0, 0.0);
    Geographic ne1(42.0, -73.0, 0.0);
    GeographicBounds bounds1(sw1, ne1);

    // Overlapping bounds
    Geographic sw2(41.0, -74.0, 0.0);
    Geographic ne2(43.0, -72.0, 0.0);
    GeographicBounds bounds2(sw2, ne2);
    EXPECT_TRUE(bounds1.Intersects(bounds2));

    // Non-overlapping bounds
    Geographic sw3(43.0, -75.0, 0.0);
    Geographic ne3(44.0, -73.0, 0.0);
    GeographicBounds bounds3(sw3, ne3);
    EXPECT_FALSE(bounds1.Intersects(bounds3));
}

// ============================================================================
// Screen Tests
// ============================================================================

class ScreenTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(ScreenTest, DefaultConstructor_CreatesOrigin) {
    Screen screen;
    EXPECT_DOUBLE_EQ(0.0, screen.x);
    EXPECT_DOUBLE_EQ(0.0, screen.y);
    EXPECT_TRUE(screen.IsValid());
}

TEST_F(ScreenTest, ValidCoordinates_PassValidation) {
    Screen screen(100.0, 200.0);
    EXPECT_TRUE(screen.IsValid());
    EXPECT_DOUBLE_EQ(100.0, screen.x);
    EXPECT_DOUBLE_EQ(200.0, screen.y);
}

TEST_F(ScreenTest, NegativeCoordinates_FailValidation) {
    Screen invalid(-10.0, 20.0);
    EXPECT_FALSE(invalid.IsValid());

    Screen invalid2(10.0, -20.0);
    EXPECT_FALSE(invalid2.IsValid());
}

TEST_F(ScreenTest, EqualityOperators_WorkCorrectly) {
    Screen s1(100.0, 200.0);
    Screen s2(100.0, 200.0);
    Screen s3(100.0, 201.0);

    EXPECT_TRUE(s1 == s2);
    EXPECT_FALSE(s1 == s3);
}

// ============================================================================
// ScreenBounds Tests
// ============================================================================

class ScreenBoundsTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(ScreenBoundsTest, ValidBounds_PassValidation) {
    Screen tl(10.0, 10.0);
    Screen br(100.0, 100.0);
    ScreenBounds bounds(tl, br);

    EXPECT_TRUE(bounds.IsValid());
    EXPECT_DOUBLE_EQ(90.0, bounds.Width());
    EXPECT_DOUBLE_EQ(90.0, bounds.Height());
}

TEST_F(ScreenBoundsTest, Contains_DetectsPointInside) {
    Screen tl(10.0, 10.0);
    Screen br(100.0, 100.0);
    ScreenBounds bounds(tl, br);

    Screen inside(50.0, 50.0);
    EXPECT_TRUE(bounds.Contains(inside));

    Screen outside(200.0, 50.0);
    EXPECT_FALSE(bounds.Contains(outside));
}

// ============================================================================
// Projected Tests
// ============================================================================

class ProjectedTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(ProjectedTest, DefaultConstructor_CreatesOrigin) {
    Projected proj;
    EXPECT_DOUBLE_EQ(0.0, proj.x);
    EXPECT_DOUBLE_EQ(0.0, proj.y);
    EXPECT_TRUE(proj.IsValid());
}

TEST_F(ProjectedTest, ValidCoordinates_PassValidation) {
    Projected proj(1000000.0, 2000000.0);
    EXPECT_TRUE(proj.IsValid());
}

TEST_F(ProjectedTest, OutOfRange_FailsValidation) {
    constexpr double HALF_WORLD = 20037508.342789244;

    Projected too_far_east(HALF_WORLD + 1.0, 0.0);
    EXPECT_FALSE(too_far_east.IsValid());

    Projected too_far_west(-HALF_WORLD - 1.0, 0.0);
    EXPECT_FALSE(too_far_west.IsValid());
}

// ============================================================================
// World Tests
// ============================================================================

class WorldTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(WorldTest, DefaultConstructor_CreatesOrigin) {
    World world;
    EXPECT_FLOAT_EQ(0.0f, world.position.x);
    EXPECT_FLOAT_EQ(0.0f, world.position.y);
    EXPECT_FLOAT_EQ(0.0f, world.position.z);
}

TEST_F(WorldTest, Distance_CalculatesCorrectly) {
    World world(3.0f, 4.0f, 0.0f);
    EXPECT_FLOAT_EQ(5.0f, world.Distance());  // 3-4-5 triangle
}

TEST_F(WorldTest, Direction_NormalizesPosition) {
    World world(3.0f, 4.0f, 0.0f);
    glm::vec3 dir = world.Direction();
    float length = glm::length(dir);
    EXPECT_NEAR(1.0f, length, 1e-6f);
}

TEST_F(WorldTest, IsOnGlobeSurface_DetectsCorrectly) {
    World on_surface(1.0f, 0.0f, 0.0f);
    EXPECT_TRUE(on_surface.IsOnGlobeSurface());

    World not_on_surface(2.0f, 0.0f, 0.0f);
    EXPECT_FALSE(not_on_surface.IsOnGlobeSurface());

    World close_enough(1.005f, 0.0f, 0.0f);
    EXPECT_TRUE(close_enough.IsOnGlobeSurface(0.01f));
}

// ============================================================================
// Utility Function Tests
// ============================================================================

class CoordinateSpacesUtilityTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(CoordinateSpacesUtilityTest, CalculateGreatCircleDistance_NYC_to_London) {
    Geographic nyc(40.7128, -74.0060, 0.0);
    Geographic london(51.5074, -0.1278, 0.0);

    double distance = CalculateGreatCircleDistance(nyc, london);

    // Expected distance: approximately 5,570 km
    EXPECT_NEAR(5570000.0, distance, 10000.0);  // Within 10 km tolerance
}

TEST_F(CoordinateSpacesUtilityTest, CalculateGreatCircleDistance_SamePoint_ReturnsZero) {
    Geographic point(40.7128, -74.0060, 0.0);
    double distance = CalculateGreatCircleDistance(point, point);

    EXPECT_NEAR(0.0, distance, 1.0);  // Within 1 meter
}

TEST_F(CoordinateSpacesUtilityTest, CalculateBearing_North) {
    Geographic start(40.0, 0.0, 0.0);
    Geographic end(41.0, 0.0, 0.0);  // Due north

    double bearing = CalculateBearing(start, end);
    EXPECT_NEAR(0.0, bearing, 1.0);  // 0° = North
}

TEST_F(CoordinateSpacesUtilityTest, CalculateBearing_East) {
    Geographic start(0.0, 0.0, 0.0);  // Equator
    Geographic end(0.0, 1.0, 0.0);    // 1° east

    double bearing = CalculateBearing(start, end);
    EXPECT_NEAR(90.0, bearing, 1.0);  // 90° = East
}

TEST_F(CoordinateSpacesUtilityTest, CalculateDestination_RoundTrip) {
    Geographic start(40.7128, -74.0060, 0.0);
    double bearing = 45.0;  // Northeast
    double distance = 100000.0;  // 100 km

    Geographic destination = CalculateDestination(start, bearing, distance);

    // Verify destination is valid
    EXPECT_TRUE(destination.IsValid());

    // Verify distance is approximately correct (round-trip check)
    double actual_distance = CalculateGreatCircleDistance(start, destination);
    EXPECT_NEAR(distance, actual_distance, 100.0);  // Within 100 meters
}

TEST_F(CoordinateSpacesUtilityTest, CalculateDestination_InvalidInput_ReturnsInvalid) {
    Geographic start(40.7128, -74.0060, 0.0);

    // Negative distance
    Geographic result = CalculateDestination(start, 0.0, -1000.0);
    EXPECT_FALSE(result.IsValid());

    // Invalid start point
    Geographic invalid_start(91.0, 0.0, 0.0);
    result = CalculateDestination(invalid_start, 0.0, 1000.0);
    EXPECT_FALSE(result.IsValid());
}

// ============================================================================
// WorldFrustum Tests
// ============================================================================

class WorldFrustumTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a simple view frustum for testing
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, 3.0f),  // Camera position
            glm::vec3(0.0f, 0.0f, 0.0f),  // Look at origin
            glm::vec3(0.0f, 1.0f, 0.0f)   // Up vector
        );

        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),  // FOV
            1.0f,                 // Aspect ratio
            0.1f,                 // Near plane
            10.0f                 // Far plane
        );

        frustum = WorldFrustum::FromMatrix(projection * view);
    }

    WorldFrustum frustum;
};

TEST_F(WorldFrustumTest, Contains_DetectsPointInside) {
    // Point at origin should be inside frustum
    World origin(0.0f, 0.0f, 0.0f);
    EXPECT_TRUE(frustum.Contains(origin));

    // Point far behind camera should be outside
    World behind(0.0f, 0.0f, 100.0f);
    EXPECT_FALSE(frustum.Contains(behind));
}

TEST_F(WorldFrustumTest, Intersects_DetectsSphereIntersection) {
    // Sphere at origin should intersect frustum
    World origin(0.0f, 0.0f, 0.0f);
    EXPECT_TRUE(frustum.Intersects(origin, 1.0f));

    // Small sphere far away should not intersect
    World far_away(0.0f, 100.0f, 0.0f);
    EXPECT_FALSE(frustum.Intersects(far_away, 0.1f));
}
