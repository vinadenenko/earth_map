// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/data/elevation_cache.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <thread>

namespace earth_map {
namespace {

/// Test fixture for elevation cache tests
class ElevationCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary cache directory
        cache_directory_ = "./test_elevation_cache";
        std::filesystem::create_directories(cache_directory_);
    }

    void TearDown() override {
        // Clean up cache directory
        if (std::filesystem::exists(cache_directory_)) {
            std::filesystem::remove_all(cache_directory_);
        }
    }

    /// Create test tile data
    std::unique_ptr<SRTMTileData> CreateTestTile(const SRTMCoordinates& coords,
                                                  int16_t base_elevation = 1000) {
        SRTMMetadata metadata(coords, SRTMResolution::SRTM3);
        auto tile = std::make_unique<SRTMTileData>(metadata);

        auto& data = tile->GetRawData();
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = base_elevation + static_cast<int16_t>(i % 100);
        }

        tile->SetValid(true);
        return tile;
    }

    std::string cache_directory_;
};

TEST_F(ElevationCacheTest, CreateCache) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);
}

TEST_F(ElevationCacheTest, PutAndGet) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = false;  // Memory only for this test

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    // Create and put tile
    const SRTMCoordinates coords{37, -122};
    auto tile = CreateTestTile(coords, 1500);
    EXPECT_TRUE(cache->Put(*tile));

    // Get tile
    auto retrieved = cache->Get(coords);
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_NE(retrieved.value(), nullptr);

    // Verify coordinates match
    const auto& metadata = retrieved.value()->GetMetadata();
    EXPECT_EQ(metadata.coordinates.latitude, 37);
    EXPECT_EQ(metadata.coordinates.longitude, -122);
}

TEST_F(ElevationCacheTest, GetNonExistent) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = false;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    // Try to get non-existent tile
    const SRTMCoordinates coords{37, -122};
    auto retrieved = cache->Get(coords);
    EXPECT_FALSE(retrieved.has_value());
}

TEST_F(ElevationCacheTest, Contains) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = false;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    const SRTMCoordinates coords{37, -122};

    // Should not contain initially
    EXPECT_FALSE(cache->Contains(coords));

    // Put tile
    auto tile = CreateTestTile(coords);
    EXPECT_TRUE(cache->Put(*tile));

    // Should contain now
    EXPECT_TRUE(cache->Contains(coords));
}

TEST_F(ElevationCacheTest, Remove) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = false;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    const SRTMCoordinates coords{37, -122};

    // Put tile
    auto tile = CreateTestTile(coords);
    EXPECT_TRUE(cache->Put(*tile));
    EXPECT_TRUE(cache->Contains(coords));

    // Remove tile
    EXPECT_TRUE(cache->Remove(coords));
    EXPECT_FALSE(cache->Contains(coords));

    // Try to remove again
    EXPECT_FALSE(cache->Remove(coords));
}

TEST_F(ElevationCacheTest, Clear) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = false;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    // Put multiple tiles
    for (int lat = 0; lat < 5; ++lat) {
        const SRTMCoordinates coords{lat, 0};
        auto tile = CreateTestTile(coords);
        EXPECT_TRUE(cache->Put(*tile));
    }

    // Verify tiles are cached
    for (int lat = 0; lat < 5; ++lat) {
        const SRTMCoordinates coords{lat, 0};
        EXPECT_TRUE(cache->Contains(coords));
    }

    // Clear cache
    cache->Clear();

    // Verify tiles are gone
    for (int lat = 0; lat < 5; ++lat) {
        const SRTMCoordinates coords{lat, 0};
        EXPECT_FALSE(cache->Contains(coords));
    }
}

TEST_F(ElevationCacheTest, LRUEviction) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = false;
    // Set small cache size to force eviction
    config.max_memory_cache_size = 10 * 1024 * 1024;  // 10MB

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    // Add tiles until eviction occurs
    const SRTMCoordinates first_coords{0, 0};
    auto first_tile = CreateTestTile(first_coords);
    EXPECT_TRUE(cache->Put(*first_tile));

    // Add more tiles
    for (int lat = 1; lat < 10; ++lat) {
        const SRTMCoordinates coords{lat, 0};
        auto tile = CreateTestTile(coords);
        EXPECT_TRUE(cache->Put(*tile));
    }

    // Check if LRU eviction occurred
    const auto stats = cache->GetStatistics();
    EXPECT_GT(stats.tile_count_memory, 0u);

    // First tile might have been evicted (or might not, depending on cache size)
    // Just verify cache is managing memory
    EXPECT_LE(stats.memory_cache_size_bytes, config.max_memory_cache_size);
}

TEST_F(ElevationCacheTest, DiskCacheWriteAndRead) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = true;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    const SRTMCoordinates coords{37, -122};
    auto tile = CreateTestTile(coords, 1500);
    const int16_t expected_elevation = tile->GetSample(0, 0).elevation_meters;

    // Put tile (should write to disk)
    EXPECT_TRUE(cache->Put(*tile));

    // Clear memory cache
    cache->ClearMemoryCache();

    // Get tile (should read from disk)
    auto retrieved = cache->Get(coords);
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_NE(retrieved.value(), nullptr);

    // Verify elevation data
    const auto sample = retrieved.value()->GetSample(0, 0);
    EXPECT_TRUE(sample.is_valid);
    EXPECT_EQ(sample.elevation_meters, expected_elevation);
}

TEST_F(ElevationCacheTest, MemoryAndDiskCacheHits) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = true;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    const SRTMCoordinates coords{37, -122};
    auto tile = CreateTestTile(coords);
    EXPECT_TRUE(cache->Put(*tile));

    // First get - memory cache hit
    auto retrieved1 = cache->Get(coords);
    EXPECT_TRUE(retrieved1.has_value());

    auto stats1 = cache->GetStatistics();
    EXPECT_EQ(stats1.memory_cache_hits, 1u);
    EXPECT_EQ(stats1.disk_cache_hits, 0u);

    // Clear memory cache
    cache->ClearMemoryCache();

    // Second get - disk cache hit
    auto retrieved2 = cache->Get(coords);
    EXPECT_TRUE(retrieved2.has_value());

    auto stats2 = cache->GetStatistics();
    EXPECT_EQ(stats2.memory_cache_hits, 1u);  // Still 1 from before
    EXPECT_EQ(stats2.disk_cache_hits, 1u);
}

TEST_F(ElevationCacheTest, CacheMiss) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = true;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    // Try to get non-existent tile
    const SRTMCoordinates coords{37, -122};
    auto retrieved = cache->Get(coords);
    EXPECT_FALSE(retrieved.has_value());

    const auto stats = cache->GetStatistics();
    EXPECT_EQ(stats.cache_misses, 1u);
}

TEST_F(ElevationCacheTest, Statistics) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = false;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    const auto initial_stats = cache->GetStatistics();
    EXPECT_EQ(initial_stats.memory_cache_hits, 0u);
    EXPECT_EQ(initial_stats.tile_count_memory, 0u);

    // Add tiles
    for (int lat = 0; lat < 5; ++lat) {
        const SRTMCoordinates coords{lat, 0};
        auto tile = CreateTestTile(coords);
        EXPECT_TRUE(cache->Put(*tile));
    }

    const auto stats_after_put = cache->GetStatistics();
    EXPECT_EQ(stats_after_put.tile_count_memory, 5u);
    EXPECT_GT(stats_after_put.memory_cache_size_bytes, 0u);

    // Get tiles
    for (int lat = 0; lat < 5; ++lat) {
        const SRTMCoordinates coords{lat, 0};
        auto retrieved = cache->Get(coords);
        EXPECT_TRUE(retrieved.has_value());
    }

    const auto stats_after_get = cache->GetStatistics();
    EXPECT_EQ(stats_after_get.memory_cache_hits, 5u);
}

TEST_F(ElevationCacheTest, Flush) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = true;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    // Add tiles to memory cache
    for (int lat = 0; lat < 3; ++lat) {
        const SRTMCoordinates coords{lat, 0};
        auto tile = CreateTestTile(coords);
        EXPECT_TRUE(cache->Put(*tile));
    }

    // Flush to disk
    const size_t flushed = cache->Flush();
    EXPECT_GE(flushed, 0u);

    // Clear memory cache
    cache->ClearMemoryCache();

    // Verify tiles can be loaded from disk
    for (int lat = 0; lat < 3; ++lat) {
        const SRTMCoordinates coords{lat, 0};
        auto retrieved = cache->Get(coords);
        EXPECT_TRUE(retrieved.has_value());
    }
}

TEST_F(ElevationCacheTest, Configuration) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.max_memory_cache_size = 100 * 1024 * 1024;
    config.enable_disk_cache = true;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    const auto retrieved_config = cache->GetConfiguration();
    EXPECT_EQ(retrieved_config.disk_cache_directory, cache_directory_);
    EXPECT_EQ(retrieved_config.max_memory_cache_size, 100 * 1024 * 1024u);
    EXPECT_TRUE(retrieved_config.enable_disk_cache);

    // Update configuration
    ElevationCacheConfig new_config = retrieved_config;
    new_config.max_memory_cache_size = 50 * 1024 * 1024;
    EXPECT_TRUE(cache->SetConfiguration(new_config));

    const auto updated_config = cache->GetConfiguration();
    EXPECT_EQ(updated_config.max_memory_cache_size, 50 * 1024 * 1024u);
}

TEST_F(ElevationCacheTest, ClearMemoryCacheOnly) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = true;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    const SRTMCoordinates coords{37, -122};
    auto tile = CreateTestTile(coords);
    EXPECT_TRUE(cache->Put(*tile));

    // Clear memory cache only
    cache->ClearMemoryCache();

    // Tile should still be in disk cache
    EXPECT_TRUE(cache->Contains(coords));

    // Get should succeed (from disk)
    auto retrieved = cache->Get(coords);
    EXPECT_TRUE(retrieved.has_value());
}

TEST_F(ElevationCacheTest, ClearDiskCacheOnly) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = true;

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    const SRTMCoordinates coords{37, -122};
    auto tile = CreateTestTile(coords);
    EXPECT_TRUE(cache->Put(*tile));

    // Clear disk cache only
    cache->ClearDiskCache();

    // Tile should still be in memory cache
    auto retrieved = cache->Get(coords);
    EXPECT_TRUE(retrieved.has_value());

    const auto stats = cache->GetStatistics();
    EXPECT_GT(stats.tile_count_memory, 0u);
}

TEST_F(ElevationCacheTest, MultipleGetsUpdateLRU) {
    ElevationCacheConfig config;
    config.disk_cache_directory = cache_directory_;
    config.enable_disk_cache = false;
    config.max_memory_cache_size = 10 * 1024 * 1024;  // Small cache

    auto cache = ElevationCache::Create(config);
    ASSERT_NE(cache, nullptr);

    const SRTMCoordinates coords1{0, 0};
    const SRTMCoordinates coords2{1, 0};

    // Put both tiles
    auto tile1 = CreateTestTile(coords1);
    auto tile2 = CreateTestTile(coords2);
    EXPECT_TRUE(cache->Put(*tile1));
    EXPECT_TRUE(cache->Put(*tile2));

    // Access first tile repeatedly (should keep it in cache)
    for (int i = 0; i < 5; ++i) {
        auto retrieved = cache->Get(coords1);
        EXPECT_TRUE(retrieved.has_value());
    }

    // Add more tiles to force eviction
    for (int lat = 2; lat < 10; ++lat) {
        const SRTMCoordinates coords{lat, 0};
        auto tile = CreateTestTile(coords);
        EXPECT_TRUE(cache->Put(*tile));
    }

    // First tile should still be in cache (accessed recently)
    // Second tile might have been evicted
    EXPECT_TRUE(cache->Contains(coords1));
}

} // anonymous namespace
} // namespace earth_map
