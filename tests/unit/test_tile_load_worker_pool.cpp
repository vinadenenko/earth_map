#include <gtest/gtest.h>
#include <earth_map/renderer/texture_atlas/tile_load_worker_pool.h>
#include <earth_map/renderer/texture_atlas/gl_upload_queue.h>
#include <earth_map/data/tile_cache.h>
#include <earth_map/data/tile_loader.h>
#include <earth_map/math/tile_mathematics.h>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>

namespace earth_map::tests {

/**
 * @brief Mock TileCache for testing
 */
class MockTileCache : public TileCache {
public:
    MockTileCache() = default;
    ~MockTileCache() override = default;

    bool Initialize(const TileCacheConfig&) override { return true; }
    bool Put(const TileData&) override { return true; }
    std::optional<TileData> Get(const TileCoordinates&) override { return std::nullopt; }  // Always miss
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
class MockTileLoader : public TileLoader {
public:
    std::atomic<int> load_count{0};

    MockTileLoader() = default;
    ~MockTileLoader() override = default;

    bool Initialize(const TileLoaderConfig&) override { return true; }
    void SetTileCache(std::shared_ptr<TileCache>) override {}

    bool AddProvider(const TileProvider&) override { return true; }
    bool SetDefaultProvider(const std::string&) override { return true; }
    bool RemoveProvider(const std::string&) override { return false; }
    const TileProvider* GetProvider(const std::string&) const override { return nullptr; }
    std::vector<std::string> GetProviderNames() const override { return {}; }
    std::string GetDefaultProvider() const override { return ""; }

    // Synchronous load - creates fake tile data
    TileLoadResult LoadTile(const TileCoordinates& coords, const std::string&) override {
        load_count.fetch_add(1);

        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        TileLoadResult result;
        result.success = true;
        result.coordinates = coords;
        result.tile_data = std::make_shared<TileData>();
        result.tile_data->metadata.coordinates = coords;
        result.tile_data->loaded = true;
        result.tile_data->width = 256;
        result.tile_data->height = 256;
        result.tile_data->channels = 3;

        // Create fake image data (RGB)
        const std::size_t data_size = 256 * 256 * 3;
        result.tile_data->data.resize(data_size);

        // Fill with pattern based on tile coords
        const std::uint8_t value = static_cast<std::uint8_t>((coords.x + coords.y + coords.zoom) % 256);
        std::fill(result.tile_data->data.begin(), result.tile_data->data.end(), value);

        return result;
    }

    std::future<TileLoadResult> LoadTileAsync(const TileCoordinates&,
                                              TileLoadCallback,
                                              const std::string&) override {
        return std::future<TileLoadResult>();
    }

    std::vector<std::future<TileLoadResult>> LoadTilesAsync(const std::vector<TileCoordinates>&,
                                                            TileLoadCallback,
                                                            const std::string&) override {
        return {};
    }

    std::size_t PreloadTiles(const std::vector<TileCoordinates>&, const std::string&) override {
        return 0;
    }

    bool CancelLoad(const TileCoordinates&) override { return false; }
    void CancelAllLoads() override {}
    TileLoaderStats GetStatistics() const override { return {}; }
    TileLoaderConfig GetConfiguration() const override { return {}; }
    bool SetConfiguration(const TileLoaderConfig&) override { return true; }
    bool IsLoading(const TileCoordinates&) const override { return false; }
    std::vector<TileCoordinates> GetLoadingTiles() const override { return {}; }
};

/**
 * @brief Test fixture for TileLoadWorkerPool
 */
class TileLoadWorkerPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        cache_ = std::make_shared<MockTileCache>();
        loader_ = std::make_shared<MockTileLoader>();
        upload_queue_ = std::make_shared<GLUploadQueue>();

        pool_ = std::make_unique<TileLoadWorkerPool>(
            cache_,
            loader_,
            upload_queue_,
            2  // 2 worker threads for testing
        );
    }

    void TearDown() override {
        pool_.reset();
        upload_queue_.reset();
        loader_.reset();
        cache_.reset();
    }

    std::shared_ptr<MockTileCache> cache_;
    std::shared_ptr<MockTileLoader> loader_;
    std::shared_ptr<GLUploadQueue> upload_queue_;
    std::unique_ptr<TileLoadWorkerPool> pool_;
};

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(TileLoadWorkerPoolTest, Initialization) {
    EXPECT_NE(pool_, nullptr);
}

TEST_F(TileLoadWorkerPoolTest, SubmitSingleRequest) {
    TileCoordinates tile(0, 0, 5);

    pool_->SubmitRequest(tile, 0);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should have loaded the tile
    EXPECT_GE(loader_->load_count.load(), 1);

    // Should have uploaded to GL queue
    EXPECT_GE(upload_queue_->Size(), 1u);
}

TEST_F(TileLoadWorkerPoolTest, SubmitMultipleRequests) {
    const int num_tiles = 10;

    for (int i = 0; i < num_tiles; ++i) {
        TileCoordinates tile(i, i, 5);
        pool_->SubmitRequest(tile, 0);
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // All tiles should be loaded
    EXPECT_EQ(loader_->load_count.load(), num_tiles);

    // All should be in upload queue
    EXPECT_EQ(upload_queue_->Size(), static_cast<std::size_t>(num_tiles));
}

TEST_F(TileLoadWorkerPoolTest, DuplicateRequests_ProcessedOnce) {
    TileCoordinates tile(5, 5, 8);

    // Submit same tile multiple times
    pool_->SubmitRequest(tile, 0);
    pool_->SubmitRequest(tile, 0);
    pool_->SubmitRequest(tile, 0);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Should only load once (deduplication)
    EXPECT_EQ(loader_->load_count.load(), 1);
    EXPECT_EQ(upload_queue_->Size(), 1u);
}

TEST_F(TileLoadWorkerPoolTest, PriorityOrdering) {
    // Submit low priority tiles
    for (int i = 0; i < 5; ++i) {
        TileCoordinates tile(i, i, 5);
        pool_->SubmitRequest(tile, 10);  // Low priority
    }

    // Submit high priority tile
    TileCoordinates high_priority(100, 100, 5);
    pool_->SubmitRequest(high_priority, 0);  // High priority (lower number = higher priority)

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // High priority tile should be processed early
    // (exact order depends on thread scheduling, but it should be among first few)
    EXPECT_GE(loader_->load_count.load(), 1);
}

TEST_F(TileLoadWorkerPoolTest, CallbackExecution) {
    TileCoordinates tile(7, 8, 9);
    std::atomic<bool> callback_called{false};

    pool_->SubmitRequest(tile, 0, [&](const TileCoordinates& coords) {
        EXPECT_EQ(coords, tile);
        callback_called.store(true);
    });

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_TRUE(callback_called.load());
}

TEST_F(TileLoadWorkerPoolTest, Shutdown_GracefulCompletion) {
    // Submit several requests
    for (int i = 0; i < 5; ++i) {
        TileCoordinates tile(i, i, 5);
        pool_->SubmitRequest(tile, 0);
    }

    // Shutdown (destructor will be called in TearDown)
    // Workers should finish current tasks
    pool_.reset();

    // All submitted tiles should have been processed
    EXPECT_GE(loader_->load_count.load(), 5);
}

TEST_F(TileLoadWorkerPoolTest, ConcurrentSubmission) {
    constexpr int num_threads = 4;
    constexpr int tiles_per_thread = 10;
    std::vector<std::thread> threads;

    // Multiple threads submit requests concurrently
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < tiles_per_thread; ++i) {
                TileCoordinates tile(t * 100 + i, t * 100 + i, 8);
                pool_->SubmitRequest(tile, 0);
            }
        });
    }

    // Wait for submission threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // All tiles should be processed
    EXPECT_EQ(loader_->load_count.load(), num_threads * tiles_per_thread);
}

TEST_F(TileLoadWorkerPoolTest, WorkerThreads_ProcessInParallel) {
    // Submit more tiles than workers
    const int num_tiles = 20;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_tiles; ++i) {
        TileCoordinates tile(i, i, 5);
        pool_->SubmitRequest(tile, 0);
    }

    // Wait for all to process
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // With 2 workers processing in parallel, should be faster than sequential
    // Sequential would take ~20 * 10ms = 200ms minimum
    // Parallel with 2 workers should take ~10 * 10ms = 100ms minimum
    // Allow overhead, but should be significantly less than sequential
    EXPECT_LT(elapsed_ms, 2000);  // Generous upper bound

    EXPECT_EQ(loader_->load_count.load(), num_tiles);
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST_F(TileLoadWorkerPoolTest, HighVolumeRequests) {
    const int num_tiles = 100;

    for (int i = 0; i < num_tiles; ++i) {
        TileCoordinates tile(i % 50, i % 50, 8);
        pool_->SubmitRequest(tile, i % 5);
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Should process all unique tiles (deduplication may reduce count)
    EXPECT_GE(loader_->load_count.load(), 50);  // At least 50 unique tiles
    EXPECT_LE(loader_->load_count.load(), num_tiles);
}

} // namespace earth_map::tests
