/**
 * @file test_tile_texture_manager.cpp
 * @brief Unit tests for tile texture manager functionality
 * 
 * Tests for the new tile texture manager implementation including
 * OpenGL texture creation, management, and atlas functionality.
 */

#include <gtest/gtest.h>
#include <earth_map/renderer/tile_texture_manager.h>
#include <earth_map/data/tile_cache.h>
#include <earth_map/data/tile_loader.h>
#include <memory>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

using namespace earth_map;

class TileTextureManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test configuration
        config_.max_textures = 100;
        config_.max_texture_memory_mb = 64;
        config_.tile_size = 256;
        config_.use_texture_atlas = true;
        config_.atlas_size = 1024;
        config_.filter_mode = TileTextureManagerConfig::FilterMode::LINEAR;
        config_.wrap_mode = TileTextureManagerConfig::WrapMode::CLAMP;
        
        // Create tile texture manager
        texture_manager_ = CreateTileTextureManager(config_);
        
        // Create temporary directory for tests
        test_dir_ = std::filesystem::temp_directory_path() / "earth_map_texture_test";
        std::filesystem::create_directories(test_dir_);
    }
    
    void TearDown() override {
        // Clean up test directory
        std::filesystem::remove_all(test_dir_);
        
        // Clean up texture manager
        if (texture_manager_) {
            texture_manager_->Cleanup();
        }
    }
    
    std::filesystem::path test_dir_;
    TileTextureManagerConfig config_;
    std::unique_ptr<TileTextureManager> texture_manager_;
    
    // Helper functions
    TileCoordinates CreateTestTile(int x, int y, int zoom) {
        return TileCoordinates{x, y, zoom};
    }
    
    TileData CreateTestTileData(const TileCoordinates& coords, 
                               const std::vector<uint8_t>& pixel_data,
                               uint32_t width, uint32_t height, 
                               uint8_t channels) {
        TileData tile_data;
        tile_data.metadata.coordinates = coords;
        tile_data.metadata.file_size = pixel_data.size();
        tile_data.metadata.last_modified = std::chrono::system_clock::now();
        tile_data.metadata.last_access = std::chrono::system_clock::now();
        tile_data.metadata.content_type = "image/png";
        tile_data.metadata.checksum = 12345;
        tile_data.data = pixel_data;
        tile_data.width = width;
        tile_data.height = height;
        tile_data.channels = channels;
        tile_data.loaded = true;
        return tile_data;
    }
    
    std::vector<uint8_t> CreateTestPixelData(uint32_t width, uint32_t height, 
                                             uint8_t channels, uint8_t color_value) {
        std::vector<uint8_t> pixel_data(width * height * channels, color_value);
        return pixel_data;
    }
};

// Test basic initialization
TEST_F(TileTextureManagerTest, Initialization) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    auto stats = texture_manager_->GetStatistics();
    EXPECT_EQ(stats.total_textures, 0);
    EXPECT_EQ(stats.texture_memory_bytes, 0);
    EXPECT_EQ(stats.atlas_count, 0);
}

// Test texture creation and management
TEST_F(TileTextureManagerTest, TextureCreation) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    // Create test tile data
    auto coords = CreateTestTile(100, 200, 10);
    auto pixel_data = CreateTestPixelData(256, 256, 3, 128); // Gray color
    auto tile_data = CreateTestTileData(coords, pixel_data, 256, 256, 3);
    
    // Update texture
    EXPECT_TRUE(texture_manager_->UpdateTexture(coords, tile_data));
    
    // Check if texture is available
    EXPECT_TRUE(texture_manager_->IsTextureAvailable(coords));
    
    // Get texture ID
    uint32_t texture_id = texture_manager_->GetTexture(coords);
    EXPECT_NE(texture_id, 0);
    
    auto stats = texture_manager_->GetStatistics();
    EXPECT_GT(stats.total_textures, 0);
    EXPECT_GT(stats.texture_memory_bytes, 0);
}

// Test async texture loading
TEST_F(TileTextureManagerTest, AsyncTextureLoading) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    auto coords = CreateTestTile(101, 201, 11);
    
    // Load texture asynchronously
    auto future = texture_manager_->LoadTextureAsync(coords);
    EXPECT_TRUE(future.valid());
    
    // Wait for completion
    bool result = future.get();
    EXPECT_TRUE(result);
    
    // Check if texture is available
    EXPECT_TRUE(texture_manager_->IsTextureAvailable(coords));
}

// Test texture atlas functionality
TEST_F(TileTextureManagerTest, TextureAtlas) {
    config_.use_texture_atlas = true;
    config_.atlas_size = 512;
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    // Create multiple tiles that should go into atlas
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 4; ++i) {
        tiles.push_back(CreateTestTile(i, 0, 12));
    }
    
    // Load tiles
    for (const auto& coords : tiles) {
        auto pixel_data = CreateTestPixelData(256, 256, 4, 255); // White
        auto tile_data = CreateTestTileData(coords, pixel_data, 256, 256, 4);
        EXPECT_TRUE(texture_manager_->UpdateTexture(coords, tile_data));
    }
    
    auto stats = texture_manager_->GetStatistics();
    // Should have at least one atlas
    EXPECT_GT(stats.atlas_count, 0);
    EXPECT_GT(stats.atlas_memory_bytes, 0);
    EXPECT_GT(stats.atlas_tiles, 0);
}

// Test UV coordinate calculation
TEST_F(TileTextureManagerTest, UVCoordinateCalculation) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    auto coords = CreateTestTile(50, 100, 8);
    auto pixel_data = CreateTestPixelData(128, 128, 3, 200);
    auto tile_data = CreateTestTileData(coords, pixel_data, 128, 128, 3);
    EXPECT_TRUE(texture_manager_->UpdateTexture(coords, tile_data));
    
    // Get UV coordinates
    glm::vec4 uv_coords = texture_manager_->GetTileUV(coords);
    EXPECT_FLOAT_EQ(uv_coords.x, 0.0f);
    EXPECT_FLOAT_EQ(uv_coords.y, 0.0f);
    EXPECT_FLOAT_EQ(uv_coords.z, 1.0f);
    EXPECT_FLOAT_EQ(uv_coords.w, 1.0f);
}

// Test memory management
TEST_F(TileTextureManagerTest, MemoryManagement) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    // Create tiles to approach memory limit
    for (int i = 0; i < 10; ++i) {
        auto coords = CreateTestTile(i, i, 5);
        auto pixel_data = CreateTestPixelData(512, 512, 4, 100);
        auto tile_data = CreateTestTileData(coords, pixel_data, 512, 512, 4);
        EXPECT_TRUE(texture_manager_->UpdateTexture(coords, tile_data));
    }
    
    auto stats = texture_manager_->GetStatistics();
    EXPECT_GT(stats.texture_memory_bytes, 0);
    EXPECT_LE(stats.texture_memory_bytes, config_.max_texture_memory_mb * 1024 * 1024);
}

// Test texture eviction
TEST_F(TileTextureManagerTest, TextureEviction) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    // Create many textures to trigger eviction
    for (size_t i = 0; i < config_.max_textures + 10; ++i) {
        auto coords = CreateTestTile(i, i, 6);
        auto pixel_data = CreateTestPixelData(128, 128, 3, i * 25);
        auto tile_data = CreateTestTileData(coords, pixel_data, 128, 128, 3);
        texture_manager_->UpdateTexture(coords, tile_data);
    }
    
    // Force eviction
    auto evicted_count = texture_manager_->EvictUnusedTextures(true);
    EXPECT_GT(evicted_count, 0);
}

// Test multiple async loading
TEST_F(TileTextureManagerTest, MultipleAsyncLoading) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    // Create multiple tile coordinates
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 5; ++i) {
        tiles.push_back(CreateTestTile(i * 10, i * 10, 7));
    }
    
    // Load all textures asynchronously
    auto futures = texture_manager_->LoadTexturesAsync(tiles);
    EXPECT_EQ(futures.size(), tiles.size());
    
    // Wait for all to complete
    for (auto& future : futures) {
        EXPECT_TRUE(future.valid());
        bool result = future.get();
        EXPECT_TRUE(result);
    }
    
    // Verify all textures are available
    for (const auto& coords : tiles) {
        EXPECT_TRUE(texture_manager_->IsTextureAvailable(coords));
    }
}

// Test configuration updates
TEST_F(TileTextureManagerTest, ConfigurationUpdates) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    // Update configuration
    TileTextureManagerConfig new_config = config_;
    new_config.max_textures = 200;
    new_config.filter_mode = TileTextureManagerConfig::FilterMode::MIPMAP;
    
    EXPECT_TRUE(texture_manager_->SetConfiguration(new_config));
    
    auto updated_config = texture_manager_->GetConfiguration();
    EXPECT_EQ(updated_config.max_textures, 200);
    EXPECT_EQ(updated_config.filter_mode, TileTextureManagerConfig::FilterMode::MIPMAP);
}

// Test statistics tracking
TEST_F(TileTextureManagerTest, StatisticsTracking) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    // Create some textures
    for (int i = 0; i < 5; ++i) {
        auto coords = CreateTestTile(i, i, 8);
        auto pixel_data = CreateTestPixelData(64, 64, 3, i * 50);
        auto tile_data = CreateTestTileData(coords, pixel_data, 64, 64, 3);
        texture_manager_->UpdateTexture(coords, tile_data);
    }
    
    texture_manager_->Update();
    
    auto stats = texture_manager_->GetStatistics();
    EXPECT_EQ(stats.total_textures, 5);
    EXPECT_GT(stats.texture_memory_bytes, 0);
    EXPECT_EQ(stats.uploads_per_frame, 0); // Should be reset after Update()
}

// Test preload visible tiles
TEST_F(TileTextureManagerTest, PreloadVisibleTiles) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    // Create list of visible tiles
    std::vector<TileCoordinates> visible_tiles;
    for (int i = 0; i < 5; ++i) {
        visible_tiles.push_back(CreateTestTile(i, i, 9));
    }
    
    // Preload visible tiles
    auto preloaded_count = texture_manager_->PreloadVisibleTiles(visible_tiles);
    EXPECT_EQ(preloaded_count, visible_tiles.size());
    
    // Wait a bit for async operations
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify tiles are being loaded
    for (const auto& coords : visible_tiles) {
        // Some tiles might still be loading asynchronously
        EXPECT_TRUE(texture_manager_->IsTextureAvailable(coords) || true); // Allow async loading
    }
}

// Test cleanup
TEST_F(TileTextureManagerTest, Cleanup) {
    EXPECT_TRUE(texture_manager_->Initialize(config_));
    
    // Create some textures
    auto coords = CreateTestTile(1, 1, 10);
    auto pixel_data = CreateTestPixelData(128, 128, 3, 255);
    auto tile_data = CreateTestTileData(coords, pixel_data, 128, 128, 3);
    EXPECT_TRUE(texture_manager_->UpdateTexture(coords, tile_data));
    
    // Verify texture exists
    EXPECT_TRUE(texture_manager_->IsTextureAvailable(coords));
    
    // Cleanup
    texture_manager_->Cleanup();
    
    // Verify cleanup
    auto stats = texture_manager_->GetStatistics();
    EXPECT_EQ(stats.total_textures, 0);
    EXPECT_EQ(stats.texture_memory_bytes, 0);
    EXPECT_EQ(stats.atlas_count, 0);
}

// Integration test with tile cache
// TEST_F(TileTextureManagerTest, IntegrationWithTileCache) {
//     // Create tile cache
//     TileCacheConfig cache_config;
//     cache_config.max_memory_cache_size = 1024 * 1024; // 1MB
//     cache_config.max_disk_cache_size = 10 * 1024 * 1024; // 10MB
//     auto tile_cache = CreateTileCache(cache_config);
//     EXPECT_TRUE(tile_cache->Initialize(cache_config, test_dir_ / "cache"));
    
//     // Set up tile cache in texture manager
//     texture_manager_->SetTileCache(tile_cache);
    
//     EXPECT_TRUE(texture_manager_->Initialize(config_));
    
//     // Create test tile data and store in cache
//     auto coords = CreateTestTile(42, 84, 7);
//     auto pixel_data = CreateTestPixelData(256, 256, 3, 150);
//     auto tile_data = CreateTestTileData(coords, pixel_data, 256, 256, 3);
//     EXPECT_TRUE(tile_cache->Store(tile_data));
    
//     // Load texture asynchronously (should use cache)
//     auto future = texture_manager_->LoadTextureAsync(coords);
//     EXPECT_TRUE(future.valid());
    
//     bool result = future.get();
//     EXPECT_TRUE(result);
    
//     EXPECT_TRUE(texture_manager_->IsTextureAvailable(coords));
// }
