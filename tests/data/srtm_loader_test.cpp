// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/data/srtm_loader.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <thread>

namespace earth_map {
namespace {

/// Test fixture for SRTM loader tests
class SRTMLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary test directory
        test_directory_ = "./test_srtm_data";
        std::filesystem::create_directories(test_directory_);
    }

    void TearDown() override {
        // Clean up test directory
        if (std::filesystem::exists(test_directory_)) {
            std::filesystem::remove_all(test_directory_);
        }
    }

    /// Create synthetic SRTM3 HGT file
    void CreateTestHGTFile(const SRTMCoordinates& coords, int16_t base_elevation = 1000) {
        constexpr size_t samples = 1201;
        constexpr size_t total_samples = samples * samples;

        std::vector<uint8_t> data(total_samples * 2);

        for (size_t i = 0; i < total_samples; ++i) {
            const int16_t elevation = base_elevation + static_cast<int16_t>(i % 100);
            data[i * 2] = static_cast<uint8_t>((elevation >> 8) & 0xFF);
            data[i * 2 + 1] = static_cast<uint8_t>(elevation & 0xFF);
        }

        const std::string filepath = test_directory_ + "/" + FormatSRTMFilename(coords);

        std::ofstream file(filepath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    std::string test_directory_;
};

TEST_F(SRTMLoaderTest, CreateLoader) {
    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);
}

TEST_F(SRTMLoaderTest, LoadFromDiskSuccess) {
    // Create test file
    const SRTMCoordinates coords{37, -122};
    CreateTestHGTFile(coords, 1500);

    // Create loader
    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    // Load tile
    const auto result = loader->LoadTile(coords);
    EXPECT_TRUE(result.success);
    ASSERT_NE(result.tile_data, nullptr);
    EXPECT_EQ(result.coordinates.latitude, 37);
    EXPECT_EQ(result.coordinates.longitude, -122);
    EXPECT_GT(result.file_size_bytes, 0u);
    EXPECT_GT(result.load_time_ms, 0.0);
}

TEST_F(SRTMLoaderTest, LoadFromDiskNotFound) {
    // Don't create test file

    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    const SRTMCoordinates coords{37, -122};
    const auto result = loader->LoadTile(coords);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.tile_data, nullptr);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(SRTMLoaderTest, LoadInvalidCoordinates) {
    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    const SRTMCoordinates invalid_coords{-91, 0};
    const auto result = loader->LoadTile(invalid_coords);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.tile_data, nullptr);
}

TEST_F(SRTMLoaderTest, LoadTileAsync) {
    // Create test file
    const SRTMCoordinates coords{27, 86};
    CreateTestHGTFile(coords, 2000);

    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    // Load async
    auto future = loader->LoadTileAsync(coords);
    ASSERT_TRUE(future.valid());

    // Wait for result
    const auto result = future.get();
    EXPECT_TRUE(result.success);
    ASSERT_NE(result.tile_data, nullptr);
    EXPECT_EQ(result.coordinates.latitude, 27);
    EXPECT_EQ(result.coordinates.longitude, 86);
}

TEST_F(SRTMLoaderTest, LoadTileAsyncWithCallback) {
    // Create test file
    const SRTMCoordinates coords{36, -112};
    CreateTestHGTFile(coords, 1800);

    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    // Track callback invocation
    bool callback_invoked = false;
    SRTMLoadResult callback_result;

    auto callback = [&](const SRTMLoadResult& result) {
        callback_invoked = true;
        callback_result = result;
    };

    // Load async with callback
    auto future = loader->LoadTileAsync(coords, callback);
    const auto result = future.get();

    // Verify callback was invoked
    EXPECT_TRUE(callback_invoked);
    EXPECT_TRUE(callback_result.success);
}

TEST_F(SRTMLoaderTest, LoadMultipleTilesAsync) {
    // Create test files
    const std::vector<SRTMCoordinates> coords = {
        {37, -122},
        {27, 86},
        {36, -112}
    };

    for (const auto& coord : coords) {
        CreateTestHGTFile(coord, 1000);
    }

    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;
    config.max_concurrent_downloads = 3;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    // Load multiple tiles
    auto futures = loader->LoadTilesAsync(coords);
    EXPECT_EQ(futures.size(), coords.size());

    // Wait for all results
    for (auto& future : futures) {
        const auto result = future.get();
        EXPECT_TRUE(result.success);
        ASSERT_NE(result.tile_data, nullptr);
    }
}

TEST_F(SRTMLoaderTest, Statistics) {
    // Create test files
    const SRTMCoordinates coords1{37, -122};
    const SRTMCoordinates coords2{27, 86};
    CreateTestHGTFile(coords1, 1500);
    CreateTestHGTFile(coords2, 2000);

    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    // Load tiles
    const auto result1 = loader->LoadTile(coords1);
    EXPECT_TRUE(result1.success);

    const auto result2 = loader->LoadTile(coords2);
    EXPECT_TRUE(result2.success);

    // Try to load non-existent tile
    const SRTMCoordinates coords3{0, 0};
    const auto result3 = loader->LoadTile(coords3);
    EXPECT_FALSE(result3.success);

    // Check statistics
    const auto stats = loader->GetStatistics();
    EXPECT_EQ(stats.tiles_loaded, 2u);
    EXPECT_EQ(stats.tiles_failed, 1u);
    EXPECT_GT(stats.average_load_time_ms, 0.0);
}

TEST_F(SRTMLoaderTest, IsLoading) {
    // Create test file
    const SRTMCoordinates coords{37, -122};
    CreateTestHGTFile(coords, 1500);

    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    // Start async load
    auto future = loader->LoadTileAsync(coords);

    // Check if loading (may or may not still be loading depending on timing)
    // Just verify the method doesn't crash
    loader->IsLoading(coords);

    // Wait for completion
    future.get();

    // Should not be loading anymore
    EXPECT_FALSE(loader->IsLoading(coords));
}

TEST_F(SRTMLoaderTest, GetPendingLoadCount) {
    // Create test file
    const SRTMCoordinates coords{37, -122};
    CreateTestHGTFile(coords, 1500);

    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;
    config.max_concurrent_downloads = 1;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    const size_t initial_count = loader->GetPendingLoadCount();
    EXPECT_EQ(initial_count, 0u);

    // Start async load
    auto future = loader->LoadTileAsync(coords);

    // Wait briefly to allow task to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get result
    future.get();

    // Should be back to zero
    const size_t final_count = loader->GetPendingLoadCount();
    EXPECT_EQ(final_count, 0u);
}

TEST_F(SRTMLoaderTest, Configuration) {
    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;
    config.preferred_resolution = SRTMResolution::SRTM3;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    const auto retrieved_config = loader->GetConfiguration();
    EXPECT_EQ(retrieved_config.source, SRTMSource::LOCAL_DISK);
    EXPECT_EQ(retrieved_config.local_directory, test_directory_);
    EXPECT_EQ(retrieved_config.preferred_resolution, SRTMResolution::SRTM3);

    // Update configuration
    SRTMLoaderConfig new_config = retrieved_config;
    new_config.preferred_resolution = SRTMResolution::SRTM1;
    EXPECT_TRUE(loader->SetConfiguration(new_config));

    const auto updated_config = loader->GetConfiguration();
    EXPECT_EQ(updated_config.preferred_resolution, SRTMResolution::SRTM1);
}

TEST_F(SRTMLoaderTest, VerifyElevationData) {
    // Create test file with known elevation
    const SRTMCoordinates coords{37, -122};
    constexpr int16_t base_elevation = 1234;
    CreateTestHGTFile(coords, base_elevation);

    SRTMLoaderConfig config;
    config.source = SRTMSource::LOCAL_DISK;
    config.local_directory = test_directory_;

    auto loader = SRTMLoader::Create(config);
    ASSERT_NE(loader, nullptr);

    const auto result = loader->LoadTile(coords);
    ASSERT_TRUE(result.success);
    ASSERT_NE(result.tile_data, nullptr);

    // Verify elevation at sample (0, 0)
    const auto sample = result.tile_data->GetSample(0, 0);
    EXPECT_TRUE(sample.is_valid);
    EXPECT_EQ(sample.elevation_meters, base_elevation);
}

TEST_F(SRTMLoaderTest, MatchesStandardSRTMNaming) {
    EXPECT_EQ(FormatSRTMFilename({0, 0}), "N00E000.hgt");
    EXPECT_EQ(FormatSRTMFilename({1, 2}), "N01E002.hgt");
    EXPECT_EQ(FormatSRTMFilename({27, 86}), "N27E086.hgt");
    EXPECT_EQ(FormatSRTMFilename({-9, -1}), "S09W001.hgt");
}

} // anonymous namespace
} // namespace earth_map
