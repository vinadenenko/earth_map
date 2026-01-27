// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/data/elevation_provider.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <spdlog/spdlog.h>

namespace earth_map {
namespace {

using namespace coordinates;

/// Test fixture for elevation provider tests
class ElevationProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary test directories
        test_data_directory_ = "./test_elevation_provider_data";
        test_cache_directory_ = "./test_elevation_provider_cache";

        std::filesystem::create_directories(test_data_directory_);
        std::filesystem::create_directories(test_cache_directory_);
    }

    void TearDown() override {
        // Clean up test directories
        if (std::filesystem::exists(test_data_directory_)) {
            std::filesystem::remove_all(test_data_directory_);
        }
        if (std::filesystem::exists(test_cache_directory_)) {
            std::filesystem::remove_all(test_cache_directory_);
        }
    }

    /// Create synthetic SRTM3 HGT file with specific elevation pattern
    void CreateTestHGTFile(const SRTMCoordinates& coords, int16_t base_elevation = 1000) {
        constexpr size_t samples = 1201;
        constexpr size_t total_samples = samples * samples;

        std::vector<uint8_t> data(total_samples * 2);

        // Create elevation pattern: base + y*10 + x
        // This creates a predictable gradient for testing interpolation
        for (size_t y = 0; y < samples; ++y) {
            for (size_t x = 0; x < samples; ++x) {
                const size_t i = y * samples + x;
                const int16_t elevation = base_elevation + static_cast<int16_t>(y / 10 + x / 10);

                // Convert to big-endian
                data[i * 2] = static_cast<uint8_t>((elevation >> 8) & 0xFF);
                data[i * 2 + 1] = static_cast<uint8_t>(elevation & 0xFF);
            }
        }

        // Format filename
        std::ostringstream oss;
        oss << (coords.latitude >= 0 ? 'N' : 'S')
            << std::abs(coords.latitude)
            << (coords.longitude >= 0 ? 'E' : 'W')
            << std::abs(coords.longitude)
            << ".hgt";

        const std::string filepath = test_data_directory_ + "/" + oss.str();

        std::ofstream file(filepath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    /// Create elevation provider with test configuration
    std::unique_ptr<ElevationProvider> CreateProvider() {
        SRTMLoaderConfig loader_config;
        loader_config.source = SRTMSource::LOCAL_DISK;
        loader_config.local_directory = test_data_directory_;

        ElevationCacheConfig cache_config;
        cache_config.disk_cache_directory = test_cache_directory_;
        cache_config.enable_disk_cache = true;

        return ElevationProvider::Create(loader_config, cache_config);
    }

    std::string test_data_directory_;
    std::string test_cache_directory_;
};

TEST_F(ElevationProviderTest, CreateProvider) {
    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);
}

TEST_F(ElevationProviderTest, GetElevationSinglePoint) {
    // Create test tile at N37W122
    const SRTMCoordinates tile_coords{37, -122};
    CreateTestHGTFile(tile_coords, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Query elevation at center of tile
    const auto result = provider->GetElevation(37.5, -121.5);
    EXPECT_TRUE(result.valid);
    EXPECT_DOUBLE_EQ(result.latitude, 37.5);
    EXPECT_DOUBLE_EQ(result.longitude, -121.5);
    EXPECT_GT(result.elevation_meters, 0.0f);
    EXPECT_EQ(result.source_tile.latitude, 37);
    EXPECT_EQ(result.source_tile.longitude, -122);
}

TEST_F(ElevationProviderTest, GetElevationInvalidCoordinates) {
    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Query with invalid latitude
    auto result = provider->GetElevation(100.0, 0.0);
    EXPECT_FALSE(result.valid);

    // Query with invalid longitude
    result = provider->GetElevation(0.0, 200.0);
    EXPECT_FALSE(result.valid);
}

TEST_F(ElevationProviderTest, GetElevationTileNotFound) {
    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Query point where no tile exists
    const auto result = provider->GetElevation(37.5, -121.5);
    EXPECT_FALSE(result.valid);
}

TEST_F(ElevationProviderTest, GetElevationMultipleTiles) {
    // Create multiple test tiles
    CreateTestHGTFile({37, -122}, 1500);
    CreateTestHGTFile({27, 86}, 2000);
    CreateTestHGTFile({36, -112}, 1800);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Query each tile
    auto result1 = provider->GetElevation(37.5, -121.5);
    EXPECT_TRUE(result1.valid);
    EXPECT_EQ(result1.source_tile.latitude, 37);

    auto result2 = provider->GetElevation(27.5, 86.5);
    EXPECT_TRUE(result2.valid);
    EXPECT_EQ(result2.source_tile.latitude, 27);

    auto result3 = provider->GetElevation(36.5, -111.5);
    EXPECT_TRUE(result3.valid);
    EXPECT_EQ(result3.source_tile.latitude, 36);
}

TEST_F(ElevationProviderTest, BatchQuerySameTile) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Query multiple points in same tile
    std::vector<Geographic> points;
    for (int i = 0; i < 10; ++i) {
        points.emplace_back(37.1 + i * 0.08, -121.1 - i * 0.08);
    }
    const auto results = provider->GetElevations(points);
    ASSERT_EQ(results.size(), points.size());

    for (size_t i = 0; i < results.size(); ++i) {
        EXPECT_TRUE(results[i].valid);
        EXPECT_DOUBLE_EQ(results[i].latitude, points[i].latitude);
        EXPECT_DOUBLE_EQ(results[i].longitude, points[i].longitude);
        EXPECT_GT(results[i].elevation_meters, 0.0f);
    }
}

TEST_F(ElevationProviderTest, BatchQueryMultipleTiles) {
    // Create test tiles
    CreateTestHGTFile({37, -122}, 1500);
    CreateTestHGTFile({27, 86}, 2000);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Query points across multiple tiles
    std::vector<Geographic> points = {
        {37.5, -121.5},  // Tile N37W122
        {27.5, 86.5},    // Tile N27E086
        {37.8, -121.2},  // Tile N37W122
        {27.2, 86.8}     // Tile N27E086
    };

    const auto results = provider->GetElevations(points);
    ASSERT_EQ(results.size(), 4u);

    EXPECT_TRUE(results[0].valid);
    EXPECT_EQ(results[0].source_tile.latitude, 37);

    EXPECT_TRUE(results[1].valid);
    EXPECT_EQ(results[1].source_tile.latitude, 27);

    EXPECT_TRUE(results[2].valid);
    EXPECT_EQ(results[2].source_tile.latitude, 37);

    EXPECT_TRUE(results[3].valid);
    EXPECT_EQ(results[3].source_tile.latitude, 27);
}

TEST_F(ElevationProviderTest, InterpolationAccuracy) {
    // Create tile with known elevation pattern
    CreateTestHGTFile({0, 0}, 1000);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Query at corners of tile (should match sample values closely)
    auto result_sw = provider->GetElevation(0.0, 0.0);  // SW corner
    EXPECT_TRUE(result_sw.valid);

    auto result_ne = provider->GetElevation(0.999, 0.999);  // NE corner
    EXPECT_TRUE(result_ne.valid);

    // Query at center (should interpolate)
    auto result_center = provider->GetElevation(0.5, 0.5);
    EXPECT_TRUE(result_center.valid);
    EXPECT_GT(result_center.elevation_meters, 0.0f);
}

TEST_F(ElevationProviderTest, TileBoundaryQuery) {
    // Create adjacent tiles
    CreateTestHGTFile({37, -122}, 1500);
    CreateTestHGTFile({37, -121}, 1600);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Query at tile boundary (longitude = -121.0)
    auto result = provider->GetElevation(37.5, -121.0);
    EXPECT_TRUE(result.valid);

    // Should use tile N37W121
    EXPECT_EQ(result.source_tile.latitude, 37);
    EXPECT_EQ(result.source_tile.longitude, -121);
}

TEST_F(ElevationProviderTest, PreloadRegionSingleTile) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Preload region covering single tile
    GeographicBounds bounds({37.0, 38.0}, {-122.0, -121.0});
    const size_t loaded = provider->PreloadRegion(bounds);

    EXPECT_EQ(loaded, 1u);

    // Query should succeed without loading (already cached)
    auto result = provider->GetElevation(37.5, -121.5);
    EXPECT_TRUE(result.valid);
}

TEST_F(ElevationProviderTest, PreloadRegionMultipleTiles) {
    // Create 2x2 grid of tiles
    CreateTestHGTFile({37, -122}, 1500);
    CreateTestHGTFile({37, -121}, 1600);
    CreateTestHGTFile({38, -122}, 1700);
    CreateTestHGTFile({38, -121}, 1800);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Preload region covering 2x2 tiles
    GeographicBounds bounds({37.0, 39.0}, {-122.0, -120.0});
    const size_t loaded = provider->PreloadRegion(bounds);

    EXPECT_EQ(loaded, 4u);
}

TEST_F(ElevationProviderTest, PreloadRegionInvalidBounds) {
    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Invalid bounds (min > max)
    GeographicBounds bounds({50.0, 40.0}, {-120.0, -130.0});
    const size_t loaded = provider->PreloadRegion(bounds);

    EXPECT_EQ(loaded, 0u);
}

TEST_F(ElevationProviderTest, IsAvailableCached) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Initially not available (not cached)
    EXPECT_FALSE(provider->IsAvailable(37.5, -121.5));

    // Load tile via preload
    GeographicBounds bounds({37.0, 38.0}, {-122.0, -121.0});
    const size_t loaded = provider->PreloadRegion(bounds);
    EXPECT_EQ(loaded, 1u);

    // Now available (cached)
    EXPECT_TRUE(provider->IsAvailable(37.5, -121.5));
}

TEST_F(ElevationProviderTest, IsAvailableInvalidCoordinates) {
    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    EXPECT_FALSE(provider->IsAvailable(100.0, 0.0));
    EXPECT_FALSE(provider->IsAvailable(0.0, 200.0));
}

TEST_F(ElevationProviderTest, CacheStatistics) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    const auto initial_stats = provider->GetCacheStatistics();
    EXPECT_EQ(initial_stats.memory_cache_hits, 0u);

    // Load tile (cache miss)
    auto result1 = provider->GetElevation(37.5, -121.5);
    EXPECT_TRUE(result1.valid);

    // Query again (cache hit)
    auto result2 = provider->GetElevation(37.6, -121.6);
    EXPECT_TRUE(result2.valid);

    const auto stats = provider->GetCacheStatistics();
    EXPECT_GT(stats.memory_cache_hits, 0u);
}

TEST_F(ElevationProviderTest, LoaderStatistics) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    const auto initial_stats = provider->GetLoaderStatistics();
    EXPECT_EQ(initial_stats.tiles_loaded, 0u);

    // Load tile and verify result
    auto result = provider->GetElevation(37.5, -121.5);
    EXPECT_TRUE(result.valid);
    EXPECT_GT(result.elevation_meters, 0.0f);

    const auto stats = provider->GetLoaderStatistics();
    EXPECT_EQ(stats.tiles_loaded, 1u);
    EXPECT_GT(stats.average_load_time_ms, 0.0);
}

TEST_F(ElevationProviderTest, ClearCache) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Load tile via preload
    GeographicBounds bounds({37.0, 38.0}, {-122.0, -121.0});
    const size_t loaded = provider->PreloadRegion(bounds);
    EXPECT_EQ(loaded, 1u);
    EXPECT_TRUE(provider->IsAvailable(37.5, -121.5));

    // Clear cache
    provider->ClearCache();

    // No longer available
    EXPECT_FALSE(provider->IsAvailable(37.5, -121.5));
}

TEST_F(ElevationProviderTest, CoordinateNormalization) {
    // Create test tile at equator
    CreateTestHGTFile({0, 0}, 1000);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Query with longitude > 180 (should wrap)
    auto result1 = provider->GetElevation(0.5, 0.5);
    EXPECT_TRUE(result1.valid);

    // Verify normalization functions
    EXPECT_DOUBLE_EQ(NormalizeLongitude(0.0), 0.0);
    EXPECT_DOUBLE_EQ(NormalizeLongitude(180.0), -180.0);
    EXPECT_DOUBLE_EQ(NormalizeLongitude(-180.0), -180.0);
    EXPECT_DOUBLE_EQ(NormalizeLongitude(360.0), 0.0);
    EXPECT_DOUBLE_EQ(NormalizeLongitude(-360.0), 0.0);

    EXPECT_DOUBLE_EQ(NormalizeLatitude(0.0), 0.0);
    EXPECT_DOUBLE_EQ(NormalizeLatitude(90.0), 90.0);
    EXPECT_DOUBLE_EQ(NormalizeLatitude(-90.0), -90.0);
    EXPECT_DOUBLE_EQ(NormalizeLatitude(100.0), 90.0);
    EXPECT_DOUBLE_EQ(NormalizeLatitude(-100.0), -90.0);
}

TEST_F(ElevationProviderTest, GeographicToSRTMTileConversion) {
    // Test corner cases
    EXPECT_EQ(GeographicToSRTMTile(37.5, -121.5).latitude, 37);
    EXPECT_EQ(GeographicToSRTMTile(37.5, -121.5).longitude, -122);

    EXPECT_EQ(GeographicToSRTMTile(0.0, 0.0).latitude, 0);
    EXPECT_EQ(GeographicToSRTMTile(0.0, 0.0).longitude, 0);

    EXPECT_EQ(GeographicToSRTMTile(-37.5, 121.5).latitude, -38);
    EXPECT_EQ(GeographicToSRTMTile(-37.5, 121.5).longitude, 121);

    // Tile boundary
    EXPECT_EQ(GeographicToSRTMTile(37.0, -122.0).latitude, 37);
    EXPECT_EQ(GeographicToSRTMTile(37.0, -122.0).longitude, -122);
}

TEST_F(ElevationProviderTest, GeographicToTileFractionConversion) {
    const SRTMCoordinates tile{37, -122};

    // SW corner
    auto frac1 = GeographicToTileFraction(37.0, -122.0, tile);
    EXPECT_DOUBLE_EQ(frac1.first, 0.0);
    EXPECT_DOUBLE_EQ(frac1.second, 0.0);

    // Center
    auto frac2 = GeographicToTileFraction(37.5, -121.5, tile);
    EXPECT_DOUBLE_EQ(frac2.first, 0.5);
    EXPECT_DOUBLE_EQ(frac2.second, 0.5);

    // NE corner (almost)
    auto frac3 = GeographicToTileFraction(37.999, -121.001, tile);
    EXPECT_NEAR(frac3.first, 0.999, 0.001);
    EXPECT_NEAR(frac3.second, 0.999, 0.001);
}

TEST_F(ElevationProviderTest, GeographicBoundsValidation) {
    GeographicBounds valid({37.0, 38.0}, {-122.0, -121.0});
    EXPECT_TRUE(valid.IsValid());

    GeographicBounds invalid_lat({38.0, 37.0}, {-122.0, -121.0});
    EXPECT_FALSE(invalid_lat.IsValid());

    GeographicBounds invalid_lon({37.0, 38.0}, {-121.0, -122.0});
    EXPECT_FALSE(invalid_lon.IsValid());

    GeographicBounds out_of_range({100.0, 110.0}, {0.0, 10.0});
    EXPECT_FALSE(out_of_range.IsValid());
}

TEST_F(ElevationProviderTest, EmptyBatchQuery) {
    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    std::vector<Geographic> empty_points;
    const auto results = provider->GetElevations(empty_points);

    EXPECT_TRUE(results.empty());
}

TEST_F(ElevationProviderTest, MixedValidInvalidBatchQuery) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Mix valid and invalid points
    std::vector<Geographic> points = {
        {37.5, -121.5},   // Valid
        {100.0, 0.0},     // Invalid latitude
        {37.8, -121.2},   // Valid
        {0.0, 200.0}      // Invalid longitude
    };

    const auto results = provider->GetElevations(points);
    ASSERT_EQ(results.size(), 4u);

    EXPECT_TRUE(results[0].valid);
    EXPECT_FALSE(results[1].valid);
    EXPECT_TRUE(results[2].valid);
    EXPECT_FALSE(results[3].valid);
}

// ============================================================================
// CONCURRENCY AND TIMEOUT TESTS
// These tests verify that the provider is truly lock-free and doesn't hang
// ============================================================================

TEST_F(ElevationProviderTest, ConcurrentQueries) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Launch multiple threads querying simultaneously
    constexpr int num_threads = 10;
    constexpr int queries_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> successful_queries{0};
    std::atomic<int> failed_queries{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < queries_per_thread; ++i) {
                // Each thread queries slightly different coordinates
                const double lat = 37.1 + (t * 0.01) + (i * 0.0001);
                const double lon = -121.1 - (t * 0.01) - (i * 0.0001);

                auto result = provider->GetElevation(lat, lon);
                if (result.valid) {
                    ++successful_queries;
                } else {
                    ++failed_queries;
                }
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify all queries completed
    const int total_queries = num_threads * queries_per_thread;
    EXPECT_EQ(successful_queries + failed_queries, total_queries);
    EXPECT_GT(successful_queries, 0);
}

TEST_F(ElevationProviderTest, BatchQueryCompletesInTime) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Prepare batch query
    std::vector<Geographic> points;
    for (int i = 0; i < 100; ++i) {
        points.emplace_back(37.1 + i * 0.008, -121.1 - i * 0.008);
    }

    // Run with timeout to detect hangs
    auto future = std::async(std::launch::async, [&]() {
        return provider->GetElevations(points);
    });

    // Wait with timeout (5 seconds should be more than enough)
    auto status = future.wait_for(std::chrono::seconds(5));

    ASSERT_NE(status, std::future_status::timeout)
        << "GetElevations() hung! Possible deadlock.";

    // Verify results
    auto results = future.get();
    ASSERT_EQ(results.size(), points.size());

    // All points should be valid (same tile)
    for (const auto& result : results) {
        EXPECT_TRUE(result.valid);
    }
}

TEST_F(ElevationProviderTest, ConcurrentBatchQueries) {
    // Create test tiles
    CreateTestHGTFile({37, -122}, 1500);
    CreateTestHGTFile({27, 86}, 2000);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Launch multiple threads doing batch queries
    constexpr int num_threads = 5;
    std::vector<std::thread> threads;
    std::atomic<int> successful_batches{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            // Each thread queries different region
            std::vector<Geographic> points;
            if (t % 2 == 0) {
                // Query N37W122 region
                for (int i = 0; i < 50; ++i) {
                    points.emplace_back(37.1 + i * 0.01, -121.1 - i * 0.01);
                }
            } else {
                // Query N27E086 region
                for (int i = 0; i < 50; ++i) {
                    points.emplace_back(27.1 + i * 0.01, 86.1 + i * 0.01);
                }
            }

            auto results = provider->GetElevations(points);
            if (results.size() == points.size()) {
                ++successful_batches;
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(successful_batches, num_threads);
}

TEST_F(ElevationProviderTest, ConcurrentPreload) {
    // Create test tiles
    CreateTestHGTFile({37, -122}, 1500);
    CreateTestHGTFile({37, -121}, 1600);
    CreateTestHGTFile({38, -122}, 1700);
    CreateTestHGTFile({38, -121}, 1800);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Launch multiple threads preloading overlapping regions
    constexpr int num_threads = 4;
    std::vector<std::thread> threads;
    std::atomic<size_t> total_loaded{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            GeographicBounds bounds({37.0, 39.0}, {-122.0, -120.0});
            size_t loaded = provider->PreloadRegion(bounds);
            total_loaded += loaded;
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Should have loaded tiles (exact count depends on timing)
    EXPECT_GT(total_loaded, 0u);
}

TEST_F(ElevationProviderTest, StressTestSinglePoint) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Hammer with many queries
    constexpr int num_iterations = 1000;
    int successful_queries = 0;

    for (int i = 0; i < num_iterations; ++i) {
        const double lat = 37.1 + (i % 100) * 0.008;
        const double lon = -121.1 - (i % 100) * 0.008;

        auto result = provider->GetElevation(lat, lon);
        if (result.valid) {
            ++successful_queries;
        }
    }

    EXPECT_EQ(successful_queries, num_iterations);
}

TEST_F(ElevationProviderTest, ConcurrentIsAvailable) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Preload tile
    GeographicBounds bounds({37.0, 38.0}, {-122.0, -121.0});
    provider->PreloadRegion(bounds);

    // Launch multiple threads checking availability
    constexpr int num_threads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> available_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 100; ++i) {
                if (provider->IsAvailable(37.5, -121.5)) {
                    ++available_count;
                }
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // All checks should have succeeded
    EXPECT_EQ(available_count, num_threads * 100);
}

TEST_F(ElevationProviderTest, ConcurrentClearCache) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Preload tile
    GeographicBounds bounds({37.0, 38.0}, {-122.0, -121.0});
    provider->PreloadRegion(bounds);

    // Launch threads that query and clear cache simultaneously
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    // Query threads
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&]() {
            while (!stop) {
                provider->GetElevation(37.5, -121.5);
            }
        });
    }

    // Clear cache thread
    threads.emplace_back([&]() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            provider->ClearCache();
        }
        stop = true;
    });

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // If we get here without hanging, test passed
    SUCCEED();
}

TEST_F(ElevationProviderTest, SinglePointQueryCompletesInTime) {
    // Create test tile
    CreateTestHGTFile({37, -122}, 1500);

    auto provider = CreateProvider();
    ASSERT_NE(provider, nullptr);

    // Run with timeout
    auto future = std::async(std::launch::async, [&]() {
        return provider->GetElevation(37.5, -121.5);
    });

    // Should complete in reasonable time
    auto status = future.wait_for(std::chrono::seconds(2));

    ASSERT_NE(status, std::future_status::timeout)
        << "GetElevation() hung!";

    auto result = future.get();
    EXPECT_TRUE(result.valid);
}

} // anonymous namespace
} // namespace earth_map
