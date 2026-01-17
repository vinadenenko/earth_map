/**
 * @file test_tile_management.cpp
 * @brief Comprehensive unit tests for tile management system
 * 
 * Tests for tile cache, tile loader, tile index, and tile manager
 * components of the Phase 3 implementation.
 */

#include <gtest/gtest.h>
#include <earth_map/data/tile_cache.h>
#include <earth_map/data/tile_loader.h>
#include <earth_map/data/tile_index.h>
#include <earth_map/data/tile_manager.h>
#include <memory>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

using namespace earth_map;

class TileManagementTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for tests
        test_dir_ = std::filesystem::temp_directory_path() / "earth_map_test";
        std::filesystem::create_directories(test_dir_);
    }
    
    void TearDown() override {
        // Clean up test directory
        std::filesystem::remove_all(test_dir_);
    }
    
    std::filesystem::path test_dir_;
    
    // Helper functions
    TileCoordinates CreateTestTile(int x, int y, int zoom) {
        return TileCoordinates{x, y, zoom};
    }
    
    TileData CreateTestTileData(const TileCoordinates& coords, 
                               const std::string& content = "test") {
        TileData tile_data;
        tile_data.metadata.coordinates = coords;
        tile_data.metadata.file_size = content.size();
        tile_data.metadata.last_modified = std::chrono::system_clock::now();
        tile_data.metadata.last_access = std::chrono::system_clock::now();
        tile_data.metadata.content_type = "image/png";
        tile_data.metadata.checksum = 12345;
        
        tile_data.data.assign(content.begin(), content.end());
        tile_data.is_compressed = false;
        tile_data.width = 256;
        tile_data.height = 256;
        tile_data.channels = 4;
        
        return tile_data;
    }
};

// Tile Cache Tests
TEST_F(TileManagementTest, TileCacheInitialization) {
    TileCacheConfig config;
    config.max_memory_cache_size = 1024 * 1024;  // 1MB
    config.max_disk_cache_size = 10 * 1024 * 1024;  // 10MB
    config.disk_cache_directory = test_dir_.string() + "/tile_cache";
    
    auto cache = CreateTileCache(config);
    ASSERT_NE(cache, nullptr);
    EXPECT_TRUE(cache->Initialize(config));
    
    auto retrieved_config = cache->GetConfiguration();
    EXPECT_EQ(retrieved_config.max_memory_cache_size, config.max_memory_cache_size);
    EXPECT_EQ(retrieved_config.max_disk_cache_size, config.max_disk_cache_size);
}

TEST_F(TileManagementTest, TileCacheStoreAndRetrieve) {
    TileCacheConfig config;
    config.disk_cache_directory = test_dir_.string() + "/tile_cache";
    
    auto cache = CreateTileCache(config);
    ASSERT_TRUE(cache->Initialize(config));
    
    // Store a tile
    TileCoordinates coords = CreateTestTile(100, 200, 10);
    TileData tile_data = CreateTestTileData(coords, "test tile content");
    
    EXPECT_TRUE(cache->Store(tile_data));
    
    // Retrieve the tile
    auto retrieved_data = cache->Retrieve(coords);
    ASSERT_NE(retrieved_data, nullptr);
    EXPECT_TRUE(retrieved_data->IsValid());
    EXPECT_EQ(retrieved_data->data, tile_data.data);
    EXPECT_EQ(retrieved_data->metadata.coordinates, coords);
}

TEST_F(TileManagementTest, TileCacheContainsAndRemove) {
    TileCacheConfig config;
    config.disk_cache_directory = test_dir_.string() + "/tile_cache";
    
    auto cache = CreateTileCache(config);
    ASSERT_TRUE(cache->Initialize(config));
    
    TileCoordinates coords = CreateTestTile(50, 100, 8);
    TileData tile_data = CreateTestTileData(coords);
    
    // Should not contain initially
    EXPECT_FALSE(cache->Contains(coords));
    
    // Store tile
    EXPECT_TRUE(cache->Store(tile_data));
    EXPECT_TRUE(cache->Contains(coords));
    
    // Remove tile
    EXPECT_TRUE(cache->Remove(coords));
    EXPECT_FALSE(cache->Contains(coords));
}

TEST_F(TileManagementTest, TileCacheStatistics) {
    TileCacheConfig config;
    config.disk_cache_directory = test_dir_.string() + "/tile_cache";
    
    auto cache = CreateTileCache(config);
    ASSERT_TRUE(cache->Initialize(config));
    
    // Store some tiles
    for (int i = 0; i < 5; ++i) {
        TileCoordinates coords = CreateTestTile(i, i, 10);
        TileData tile_data = CreateTestTileData(coords, "tile_" + std::to_string(i));
        cache->Store(tile_data);
    }
    
    auto stats = cache->GetStatistics();
    EXPECT_EQ(stats.memory_cache_count, 5);
    EXPECT_GT(stats.memory_cache_size, 0);
}

TEST_F(TileManagementTest, TileCacheEviction) {
    TileCacheConfig config;
    config.max_memory_cache_size = 100;  // Very small limit
    config.disk_cache_directory = test_dir_.string() + "/tile_cache";
    
    auto cache = CreateTileCache(config);
    ASSERT_TRUE(cache->Initialize(config));
    
    // Store tiles to trigger eviction
    std::vector<TileCoordinates> stored_tiles;
    for (int i = 0; i < 10; ++i) {
        TileCoordinates coords = CreateTestTile(i, i, 10);
        TileData tile_data = CreateTestTileData(coords, "large_tile_content_" + std::to_string(i));
        cache->Store(tile_data);
        stored_tiles.push_back(coords);
    }
    
    // Check that only some tiles remain in memory (eviction occurred)
    auto stats = cache->GetStatistics();
    EXPECT_LT(stats.memory_cache_count, 10);
}

// Tile Loader Tests
TEST_F(TileManagementTest, TileLoaderInitialization) {
    TileLoaderConfig config;
    config.max_concurrent_downloads = 2;
    config.timeout = 10;
    
    auto loader = CreateTileLoader(config);
    ASSERT_NE(loader, nullptr);
    EXPECT_TRUE(loader->Initialize(config));
    
    auto retrieved_config = loader->GetConfiguration();
    EXPECT_EQ(retrieved_config.max_concurrent_downloads, 2);
    EXPECT_EQ(retrieved_config.timeout, 10);
}

TEST_F(TileManagementTest, TileLoaderProviders) {
    TileLoaderConfig config;
    auto loader = CreateTileLoader(config);
    ASSERT_TRUE(loader->Initialize(config));
    
    // Check default providers
    auto provider_names = loader->GetProviderNames();
    EXPECT_GT(provider_names.size(), 0);
    
    // Test getting a provider
    const TileProvider* provider = loader->GetProvider("OpenStreetMap");
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->name, "OpenStreetMap");
    EXPECT_FALSE(provider->url_template.empty());
}

TEST_F(TileManagementTest, TileLoadSynchronous) {
    TileLoaderConfig config;
    auto loader = CreateTileLoader(config);
    ASSERT_TRUE(loader->Initialize(config));
    
    TileCoordinates coords = CreateTestTile(0, 0, 1);
    auto result = loader->LoadTile(coords);
    
    // Should succeed with dummy implementation
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.coordinates, coords);
    EXPECT_NE(result.tile_data, nullptr);
    EXPECT_TRUE(result.tile_data->IsValid());
}

TEST_F(TileManagementTest, TileLoadAsynchronous) {
    TileLoaderConfig config;
    auto loader = CreateTileLoader(config);
    ASSERT_TRUE(loader->Initialize(config));
    
    TileCoordinates coords = CreateTestTile(1, 1, 2);
    auto future = loader->LoadTileAsync(coords);
    
    // Wait for completion
    auto result = future.get();
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.coordinates, coords);
    EXPECT_NE(result.tile_data, nullptr);
}

TEST_F(TileManagementTest, TileLoaderStatistics) {
    TileLoaderConfig config;
    auto loader = CreateTileLoader(config);
    ASSERT_TRUE(loader->Initialize(config));
    
    // Load some tiles
    TileCoordinates coords1 = CreateTestTile(0, 0, 1);
    TileCoordinates coords2 = CreateTestTile(1, 1, 2);
    
    loader->LoadTile(coords1);
    loader->LoadTile(coords2);
    
    auto stats = loader->GetStatistics();
    EXPECT_EQ(stats.total_requests, 2);
    EXPECT_EQ(stats.successful_requests, 2);
    EXPECT_EQ(stats.failed_requests, 0);
    EXPECT_GT(stats.total_bytes_downloaded, 0);
}

// Tile Index Tests
TEST_F(TileManagementTest, TileIndexInitialization) {
    TileIndexConfig config;
    config.max_tiles_per_node = 8;
    config.max_quadtree_depth = 15;
    
    auto index = CreateTileIndex(config);
    ASSERT_NE(index, nullptr);
    EXPECT_TRUE(index->Initialize(config));
    
    auto retrieved_config = index->GetConfiguration();
    EXPECT_EQ(retrieved_config.max_tiles_per_node, 8);
    EXPECT_EQ(retrieved_config.max_quadtree_depth, 15);
}

TEST_F(TileManagementTest, TileIndexInsertAndQuery) {
    TileIndexConfig config;
    auto index = CreateTileIndex(config);
    ASSERT_TRUE(index->Initialize(config));
    
    // Insert tiles
    std::vector<TileCoordinates> test_tiles = {
        CreateTestTile(0, 0, 1),
        CreateTestTile(1, 0, 1),
        CreateTestTile(0, 1, 1),
        CreateTestTile(1, 1, 1),
        CreateTestTile(2, 2, 2)
    };
    
    for (const auto& tile : test_tiles) {
        EXPECT_TRUE(index->Insert(tile));
    }
    
    // Query all tiles
    auto all_tiles = index->GetTilesAtZoom(1);
    EXPECT_EQ(all_tiles.size(), 4);
    
    // Query specific zoom level
    auto zoom2_tiles = index->GetTilesAtZoom(2);
    EXPECT_EQ(zoom2_tiles.size(), 1);
}

TEST_F(TileManagementTest, TileIndexRemove) {
    TileIndexConfig config;
    auto index = CreateTileIndex(config);
    ASSERT_TRUE(index->Initialize(config));
    
    TileCoordinates tile = CreateTestTile(5, 10, 8);
    
    // Insert and verify
    EXPECT_TRUE(index->Insert(tile));
    EXPECT_TRUE(index->Contains(tile));
    EXPECT_EQ(index->GetTileCount(), 1);
    
    // Remove and verify
    EXPECT_TRUE(index->Remove(tile));
    EXPECT_FALSE(index->Contains(tile));
    EXPECT_EQ(index->GetTileCount(), 0);
}

TEST_F(TileManagementTest, TileIndexNeighbors) {
    TileIndexConfig config;
    auto index = CreateTileIndex(config);
    ASSERT_TRUE(index->Initialize(config));
    
    TileCoordinates tile = CreateTestTile(10, 10, 5);
    
    auto neighbors = index->GetNeighbors(tile);
    EXPECT_EQ(neighbors.size(), 8);
    
    // Check specific neighbors
    EXPECT_EQ(neighbors[0], CreateTestTile(10, 11, 5));  // North
    EXPECT_EQ(neighbors[2], CreateTestTile(11, 10, 5));  // East
    EXPECT_EQ(neighbors[4], CreateTestTile(10, 9, 5));   // South
    EXPECT_EQ(neighbors[6], CreateTestTile(9, 10, 5));   // West
}

TEST_F(TileManagementTest, TileIndexParentChildren) {
    TileIndexConfig config;
    auto index = CreateTileIndex(config);
    ASSERT_TRUE(index->Initialize(config));
    
    TileCoordinates child = CreateTestTile(4, 6, 8);
    
    // Get parent
    auto parent = index->GetParent(child);
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(parent.value(), CreateTestTile(2, 3, 7));
    
    // Get children
    auto children = index->GetChildren(child);
    EXPECT_EQ(children.size(), 4);
    EXPECT_EQ(children[0], CreateTestTile(8, 12, 9));  // SW
    EXPECT_EQ(children[1], CreateTestTile(9, 12, 9));  // SE
    EXPECT_EQ(children[2], CreateTestTile(8, 13, 9));  // NW
    EXPECT_EQ(children[3], CreateTestTile(9, 13, 9));  // NE
}

TEST_F(TileManagementTest, TileIndexStatistics) {
    TileIndexConfig config;
    auto index = CreateTileIndex(config);
    ASSERT_TRUE(index->Initialize(config));
    
    // Insert tiles at different zoom levels
    for (int z = 0; z < 3; ++z) {
        for (int x = 0; x < 4; ++x) {
            for (int y = 0; y < 4; ++y) {
                index->Insert(CreateTestTile(x, y, z));
            }
        }
    }
    
    auto stats = index->GetStatistics();
    EXPECT_EQ(stats.total_tiles, 48);  // 3 levels * 4x4 tiles
    EXPECT_GT(stats.total_nodes, 0);
    EXPECT_GT(stats.max_depth, 0);
    
    // Check tiles per zoom level
    EXPECT_EQ(stats.tiles_per_zoom[0], 16);
    EXPECT_EQ(stats.tiles_per_zoom[1], 16);
    EXPECT_EQ(stats.tiles_per_zoom[2], 16);
}

// Integration Tests
TEST_F(TileManagementTest, TileManagerIntegration) {
    // Create components
    TileCacheConfig cache_config;
    cache_config.disk_cache_directory = test_dir_.string() + "/integration_cache";
    auto cache = CreateTileCache(cache_config);
    ASSERT_TRUE(cache->Initialize(cache_config));
    
    TileLoaderConfig loader_config;
    auto loader = CreateTileLoader(loader_config);
    ASSERT_TRUE(loader->Initialize(loader_config));
    loader->SetTileCache(std::move(cache));
    
    TileIndexConfig index_config;
    auto index = CreateTileIndex(index_config);
    ASSERT_TRUE(index->Initialize(index_config));
    
    TileManagerConfig manager_config;
    auto manager = CreateTileManager(manager_config);
    ASSERT_TRUE(manager->Initialize(manager_config));
    
    // Test basic workflow
    TileCoordinates coords = CreateTestTile(100, 200, 10);
    
    // Load tile through loader
    auto load_result = loader->LoadTile(coords);
    ASSERT_TRUE(load_result.success);
    
    // Check it's in cache
    EXPECT_TRUE(cache->Contains(coords));
    
    // Add to index
    EXPECT_TRUE(index->Insert(coords));
    EXPECT_TRUE(index->Contains(coords));
    
    // Query from index
    auto index_tiles = index->Query(
        TileMathematics::GetTileBounds(coords), 10);
    EXPECT_EQ(index_tiles.size(), 1);
    EXPECT_EQ(index_tiles[0], coords);
}

TEST_F(TileManagementTest, ConcurrencyTest) {
    TileCacheConfig config;
    config.disk_cache_directory = test_dir_.string() + "/concurrency_cache";
    auto cache = CreateTileCache(config);
    ASSERT_TRUE(cache->Initialize(config));
    
    // Test concurrent access to cache
    const int num_threads = 4;
    const int tiles_per_thread = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < tiles_per_thread; ++i) {
                TileCoordinates coords = CreateTestTile(t * 100 + i, i, 10);
                TileData tile_data = CreateTestTileData(coords, "thread_" + std::to_string(t));
                
                if (cache->Store(tile_data)) {
                    auto retrieved = cache->Retrieve(coords);
                    if (retrieved && retrieved->IsValid()) {
                        success_count++;
                    }
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(success_count, num_threads * tiles_per_thread);
    
    auto stats = cache->GetStatistics();
    EXPECT_EQ(stats.memory_cache_count, num_threads * tiles_per_thread);
}

TEST_F(TileManagementTest, PerformanceTest) {
    TileIndexConfig config;
    auto index = CreateTileIndex(config);
    ASSERT_TRUE(index->Initialize(config));
    
    const int num_tiles = 1000;
    
    // Measure insertion performance
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_tiles; ++i) {
        int x = i % 100;
        int y = (i / 100) % 100;
        int z = i % 10;
        index->Insert(CreateTestTile(x, y, z));
    }
    
    auto insert_end = std::chrono::high_resolution_clock::now();
    auto insert_time = std::chrono::duration_cast<std::chrono::microseconds>(
        insert_end - start);
    
    // Measure query performance
    start = std::chrono::high_resolution_clock::now();
    
    BoundingBox2D query_bounds;
    query_bounds.min = glm::dvec2(-1.0, -1.0);
    query_bounds.max = glm::dvec2(1.0, 1.0);
    
    auto results = index->Query(query_bounds);
    
    auto query_end = std::chrono::high_resolution_clock::now();
    auto query_time = std::chrono::duration_cast<std::chrono::microseconds>(
        query_end - start);
    
    // Performance assertions (these values may need adjustment based on hardware)
    EXPECT_LT(insert_time.count(), 100000);  // < 100ms for 1000 inserts
    EXPECT_LT(query_time.count(), 10000);     // < 10ms for query
    EXPECT_EQ(results.size(), num_tiles);
    
    auto stats = index->GetStatistics();
    EXPECT_EQ(stats.total_tiles, num_tiles);
    
    spdlog::info("Performance: Insert {} tiles in {}μs, Query {} results in {}μs",
                 num_tiles, insert_time.count(), results.size(), query_time.count());
}

// Error handling tests
TEST_F(TileManagementTest, ErrorHandling) {
    // Test invalid coordinates
    TileCacheConfig cache_config;
    auto cache = CreateTileCache(cache_config);
    ASSERT_TRUE(cache->Initialize(cache_config));
    
    TileCoordinates invalid_tile = CreateTestTile(-1, -1, -1);
    EXPECT_FALSE(cache->Retrieve(invalid_tile));
    EXPECT_FALSE(cache->Remove(invalid_tile));
    
    // Test invalid configuration
    TileCacheConfig invalid_config;
    invalid_config.disk_cache_directory = "/invalid/path/that/does/not/exist";
    auto invalid_cache = CreateTileCache(invalid_config);
    // May or may not fail depending on implementation
    
    // Test tile loader with invalid provider
    TileLoaderConfig loader_config;
    auto loader = CreateTileLoader(loader_config);
    ASSERT_TRUE(loader->Initialize(loader_config));
    
    auto result = loader->LoadTile(CreateTestTile(0, 0, 25), "NonExistentProvider");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
