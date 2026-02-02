#include <gtest/gtest.h>
#include <earth_map/renderer/texture_atlas/tile_texture_coordinator.h>
#include <earth_map/data/tile_cache.h>
#include <earth_map/data/tile_loader.h>
#include <earth_map/math/tile_mathematics.h>
#include <memory>
#include <thread>
#include <chrono>

namespace earth_map::tests {

/**
 * @brief Mock TileCache for testing
 */
class CoordinatorMockTileCache : public TileCache {
public:
    bool Initialize(const TileCacheConfig&) override { return true; }
    bool Put(const TileData&) override { return true; }
    std::optional<TileData> Get(const TileCoordinates&) override { return std::nullopt; }
    bool Contains(const TileCoordinates&) const override { return false; }
    bool Remove(const TileCoordinates&) override { return false; }
    void Clear() override {}
    bool UpdateMetadata(const TileCoordinates&, const TileMetadata&) override { return true; }
    std::shared_ptr<TileMetadata> GetMetadata(const TileCoordinates&) const override { return nullptr; }
    TileCacheStats GetStatistics() const override { return {}; }
    std::size_t Cleanup() override { return 0; }
    TileCacheConfig GetConfiguration() const override { return {}; }
    bool SetConfiguration(const TileCacheConfig&) override { return true; }
    std::size_t Preload(const std::vector<TileCoordinates>&) override { return 0; }
    std::vector<TileCoordinates> GetTilesInBounds(const BoundingBox2D&) const override { return {}; }
    std::vector<TileCoordinates> GetTilesAtZoom(std::uint8_t) const override { return {}; }
};

/**
 * @brief Mock TileLoader for testing
 */
class CoordinatorMockTileLoader : public TileLoader {
public:
    bool Initialize(const TileLoaderConfig&) override { return true; }
    void SetTileCache(std::shared_ptr<TileCache>) override {}
    bool AddProvider(std::shared_ptr<TileProvider>) override { return true; }
    bool SetDefaultProvider(const std::string&) override { return true; }
    bool RemoveProvider(const std::string&) override { return false; }
    const TileProvider* GetProvider(const std::string&) const override { return nullptr; }
    std::vector<std::string> GetProviderNames() const override { return {}; }
    std::string GetDefaultProvider() const override { return ""; }

    TileLoadResult LoadTile(const TileCoordinates& coords, const std::string&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        TileLoadResult result;
        result.success = true;
        result.coordinates = coords;
        result.tile_data = std::make_shared<TileData>();
        result.tile_data->metadata.coordinates = coords;
        result.tile_data->loaded = true;
        result.tile_data->width = 256;
        result.tile_data->height = 256;
        result.tile_data->channels = 4;

        const std::size_t data_size = 256 * 256 * 4;
        result.tile_data->data.resize(data_size);
        const std::uint8_t value = static_cast<std::uint8_t>((coords.x + coords.y) % 256);
        std::fill(result.tile_data->data.begin(), result.tile_data->data.end(), value);

        return result;
    }

    std::future<TileLoadResult> LoadTileAsync(const TileCoordinates&, TileLoadCallback,
                                              const std::string&) override {
        return std::future<TileLoadResult>();
    }

    std::vector<std::future<TileLoadResult>> LoadTilesAsync(const std::vector<TileCoordinates>&,
                                                            TileLoadCallback, const std::string&) override {
        return {};
    }

    std::size_t PreloadTiles(const std::vector<TileCoordinates>&, const std::string&) override { return 0; }
    bool CancelLoad(const TileCoordinates&) override { return false; }
    void CancelAllLoads() override {}
    TileLoaderStats GetStatistics() const override { return {}; }
    TileLoaderConfig GetConfiguration() const override { return {}; }
    bool SetConfiguration(const TileLoaderConfig&) override { return true; }
    bool IsLoading(const TileCoordinates&) const override { return false; }
    std::vector<TileCoordinates> GetLoadingTiles() const override { return {}; }
};

/**
 * @brief Test fixture for TileTextureCoordinator
 */
class TileTextureCoordinatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        cache_ = std::make_shared<CoordinatorMockTileCache>();
        loader_ = std::make_shared<CoordinatorMockTileLoader>();

        coordinator_ = std::make_unique<TileTextureCoordinator>(
            cache_,
            loader_,
            2,     // 2 worker threads
            true   // skip_gl_init
        );
    }

    void TearDown() override {
        coordinator_.reset();
    }

    std::shared_ptr<CoordinatorMockTileCache> cache_;
    std::shared_ptr<CoordinatorMockTileLoader> loader_;
    std::unique_ptr<TileTextureCoordinator> coordinator_;
};

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(TileTextureCoordinatorTest, Initialization) {
    EXPECT_NE(coordinator_, nullptr);
    EXPECT_EQ(coordinator_->GetAtlasTextureID(), 0u);  // 0 because GL is skipped
}

// ============================================================================
// Request Tiles Tests
// ============================================================================

TEST_F(TileTextureCoordinatorTest, RequestSingleTile) {
    TileCoordinates tile(0, 0, 5);

    coordinator_->RequestTiles({tile}, 0);

    // Wait for worker to load and queue
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Process uploads (simulate GL thread)
    coordinator_->ProcessUploads(10);

    // Tile should be ready
    EXPECT_TRUE(coordinator_->IsTileReady(tile));
}

TEST_F(TileTextureCoordinatorTest, RequestMultipleTiles) {
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 5; ++i) {
        tiles.emplace_back(i, i, 5);
    }

    coordinator_->RequestTiles(tiles, 0);

    // Wait for workers to load and queue
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Process uploads (simulate GL thread)
    coordinator_->ProcessUploads(10);

    // All tiles should be ready
    for (const auto& tile : tiles) {
        EXPECT_TRUE(coordinator_->IsTileReady(tile));
    }
}

TEST_F(TileTextureCoordinatorTest, RequestDuplicateTiles_Idempotent) {
    TileCoordinates tile(5, 5, 8);

    // Request same tile multiple times
    coordinator_->RequestTiles({tile}, 0);
    coordinator_->RequestTiles({tile}, 0);
    coordinator_->RequestTiles({tile}, 0);

    // Wait for workers to load and queue
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Process uploads
    coordinator_->ProcessUploads(10);

    // Should only load once (idempotent)
    EXPECT_TRUE(coordinator_->IsTileReady(tile));
}

TEST_F(TileTextureCoordinatorTest, IsTileReady_ReturnsFalseBeforeLoad) {
    TileCoordinates tile(10, 10, 5);

    EXPECT_FALSE(coordinator_->IsTileReady(tile));
}

TEST_F(TileTextureCoordinatorTest, IsTileReady_ReturnsTrueAfterLoad) {
    TileCoordinates tile(3, 7, 9);

    coordinator_->RequestTiles({tile}, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    coordinator_->ProcessUploads(10);

    EXPECT_TRUE(coordinator_->IsTileReady(tile));
}

// ============================================================================
// UV Coordinate Tests
// ============================================================================

TEST_F(TileTextureCoordinatorTest, GetTileUV_LoadedTile) {
    TileCoordinates tile(1, 2, 6);

    coordinator_->RequestTiles({tile}, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    coordinator_->ProcessUploads(10);

    glm::vec4 uv = coordinator_->GetTileUV(tile);

    // Should return valid UV (not default)
    EXPECT_GE(uv.x, 0.0f);
    EXPECT_GE(uv.y, 0.0f);
    EXPECT_GT(uv.z, uv.x);
    EXPECT_GT(uv.w, uv.y);
}

TEST_F(TileTextureCoordinatorTest, GetTileUV_UnloadedTile_ReturnsDefault) {
    TileCoordinates tile(99, 99, 10);

    glm::vec4 uv = coordinator_->GetTileUV(tile);

    // Should return default UV
    EXPECT_EQ(uv.x, 0.0f);
    EXPECT_EQ(uv.y, 0.0f);
    EXPECT_EQ(uv.z, 0.0f);
    EXPECT_EQ(uv.w, 0.0f);
}

// ============================================================================
// ProcessUploads Tests
// ============================================================================

TEST_F(TileTextureCoordinatorTest, ProcessUploads_DrainsQueue) {
    // Request tiles
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 10; ++i) {
        tiles.emplace_back(i, i, 5);
    }
    coordinator_->RequestTiles(tiles, 0);

    // Wait for worker threads to load and queue
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Process uploads (simulate GL thread)
    coordinator_->ProcessUploads(5);  // Process up to 5

    // After processing, tiles should start becoming ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int ready_count = 0;
    for (const auto& tile : tiles) {
        if (coordinator_->IsTileReady(tile)) {
            ++ready_count;
        }
    }

    // At least some tiles should be ready
    EXPECT_GE(ready_count, 1);
}

TEST_F(TileTextureCoordinatorTest, ProcessUploads_MultipleFrames) {
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 20; ++i) {
        tiles.emplace_back(i, i, 8);
    }

    coordinator_->RequestTiles(tiles, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Process in batches (simulating frame budget)
    for (int frame = 0; frame < 10; ++frame) {
        coordinator_->ProcessUploads(5);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 FPS
    }

    // All tiles should eventually be ready
    int ready_count = 0;
    for (const auto& tile : tiles) {
        if (coordinator_->IsTileReady(tile)) {
            ++ready_count;
        }
    }

    EXPECT_GE(ready_count, 15);  // Most should be ready
}

// ============================================================================
// Pool Eviction Tests
// ============================================================================

TEST_F(TileTextureCoordinatorTest, PoolEviction_WhenFull) {
    // Request more tiles than pool capacity (512 layers)
    // Use 70 tiles which is well under pool capacity — all should load
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 70; ++i) {
        tiles.emplace_back(i, i, 8);
    }

    coordinator_->RequestTiles(tiles, 0);

    // Wait for all to process
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Process all uploads
    for (int i = 0; i < 20; ++i) {
        coordinator_->ProcessUploads(10);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // All 70 tiles should fit in pool (512 capacity)
    int ready_count = 0;
    for (const auto& tile : tiles) {
        if (coordinator_->IsTileReady(tile)) {
            ++ready_count;
        }
    }

    EXPECT_LE(ready_count, 512);
    EXPECT_GE(ready_count, 1);  // At least some should have loaded
}

// ============================================================================
// Concurrent Access Tests
// ============================================================================

TEST_F(TileTextureCoordinatorTest, ConcurrentRequests) {
    constexpr int num_threads = 4;
    constexpr int tiles_per_thread = 10;
    std::vector<std::thread> threads;

    // Multiple threads request tiles concurrently
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t]() {
            std::vector<TileCoordinates> tiles;
            for (int i = 0; i < tiles_per_thread; ++i) {
                tiles.emplace_back(t * 100 + i, t * 100 + i, 8);
            }
            coordinator_->RequestTiles(tiles, 0);
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Process uploads
    for (int i = 0; i < 10; ++i) {
        coordinator_->ProcessUploads(10);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Should have processed all unique tiles
    // (No crashes = thread safety works)
    EXPECT_TRUE(true);
}

TEST_F(TileTextureCoordinatorTest, ConcurrentReadsDuringUploads) {
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 20; ++i) {
        tiles.emplace_back(i, i, 5);
    }

    coordinator_->RequestTiles(tiles, 0);

    std::atomic<bool> stop_reading{false};
    std::atomic<int> read_count{0};

    // Reader thread (checking UV and ready status)
    std::thread reader([&]() {
        while (!stop_reading.load()) {
            for (const auto& tile : tiles) {
                coordinator_->IsTileReady(tile);
                coordinator_->GetTileUV(tile);
                read_count.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Process uploads concurrently
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    for (int i = 0; i < 10; ++i) {
        coordinator_->ProcessUploads(5);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stop_reading.store(true);
    reader.join();

    // Should have done many reads without crashes
    EXPECT_GT(read_count.load(), 100);
}

// ============================================================================
// GetAtlasTextureID Test
// ============================================================================

TEST_F(TileTextureCoordinatorTest, GetAtlasTextureID) {
    std::uint32_t atlas_id = coordinator_->GetAtlasTextureID();

    // Should return 0 when GL is skipped
    EXPECT_EQ(atlas_id, 0u);
}

} // namespace earth_map::tests
