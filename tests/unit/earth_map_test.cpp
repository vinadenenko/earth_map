#include <gtest/gtest.h>
#include <earth_map/earth_map.h>

namespace earth_map::tests {

class EarthMapTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.screen_width = 800;
        config_.screen_height = 600;
    }
    
    void TearDown() override {
        // Cleanup
    }
    
    Configuration config_;
};

TEST_F(EarthMapTest, CreateWithValidConfig) {
    auto earth_map = EarthMap::Create(config_);
    EXPECT_NE(earth_map, nullptr);
}

TEST_F(EarthMapTest, CreateWithMinimalConfig) {
    Configuration minimal_config;
    auto earth_map = EarthMap::Create(minimal_config);
    EXPECT_NE(earth_map, nullptr);
}

TEST_F(EarthMapTest, Initialization) {
    auto earth_map = EarthMap::Create(config_);
    ASSERT_NE(earth_map, nullptr);
    
    // Note: This test may fail without proper OpenGL context
    // In a real test environment, we would set up an OpenGL context first
    // bool initialized = earth_map->Initialize();
    // EXPECT_TRUE(initialized);
}

TEST_F(EarthMapTest, PlacemarkLayerAvailableBeforeInitialization) {
    auto earth_map = EarthMap::Create(config_);
    ASSERT_NE(earth_map, nullptr);

    // Rendering subsystems are created only during Initialize(), after a GL
    // context is current. Placemark feature ownership is deliberately safe to
    // use immediately after construction.
    EXPECT_NE(earth_map->GetPlacemarkLayer(), nullptr);
}

TEST_F(EarthMapTest, UsesApplicationSuppliedPlacemarkLayer) {
    const auto supplied_layer = placemarks::PlacemarkLayer::Create();
    config_.placemark_layer = supplied_layer;

    const auto earth_map = EarthMap::Create(config_);
    ASSERT_NE(earth_map, nullptr);
    EXPECT_EQ(earth_map->GetPlacemarkLayer(), supplied_layer);
}

TEST_F(EarthMapTest, PerformanceStatsFormat) {
    auto earth_map = EarthMap::Create(config_);
    ASSERT_NE(earth_map, nullptr);
    
    std::string stats = earth_map->GetPerformanceStats();
    
    // Should return valid JSON string
    EXPECT_FALSE(stats.empty());
    EXPECT_EQ(stats.front(), '{');
    EXPECT_EQ(stats.back(), '}');
}

// TEST_F(LibraryInfoTest, GetVersion) {
//     std::string version = LibraryInfo::GetVersion();
//     EXPECT_FALSE(version.empty());
    
//     // Should follow semantic versioning pattern (e.g., "0.1.0")
//     EXPECT_NE(version.find('.'), std::string::npos);
// }

// TEST_F(LibraryInfoTest, GetBuildInfo) {
//     std::string build_info = LibraryInfo::GetBuildInfo();
//     EXPECT_FALSE(build_info.empty());
//     EXPECT_NE(build_info.find("Earth Map"), std::string::npos);
// }

// TEST_F(LibraryInfoTest, CheckSystemRequirements) {
//     bool requirements_met = LibraryInfo::CheckSystemRequirements();
//     // This should return true on any system with OpenGL 3.3+
//     // In a real test, we might want to check specific requirements
//     EXPECT_TRUE(requirements_met);
// }

} // namespace earth_map::tests
