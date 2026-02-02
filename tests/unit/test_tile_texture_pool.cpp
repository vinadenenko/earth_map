#include <gtest/gtest.h>
#include <earth_map/renderer/tile_pool/tile_texture_pool.h>
#include <earth_map/renderer/tile_pool/indirection_texture_manager.h>
#include <earth_map/math/tile_mathematics.h>
#include <optional>
#include <vector>
#include <thread>

namespace earth_map::tests {

class TileTexturePoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = std::make_unique<TileTexturePool>(
            256,   // tile_size
            64,    // max_layers (small for testing)
            true   // skip_gl_init
        );
    }

    void TearDown() override {
        pool_.reset();
    }

    std::vector<std::uint8_t> CreateTestPixelData(
        std::uint32_t width, std::uint32_t height, std::uint8_t channels) {
        const std::size_t size = width * height * channels;
        std::vector<std::uint8_t> data(size);
        for (std::size_t i = 0; i < size; ++i) {
            data[i] = static_cast<std::uint8_t>(i % 256);
        }
        return data;
    }

    std::unique_ptr<TileTexturePool> pool_;
};

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(TileTexturePoolTest, Initialization) {
    EXPECT_EQ(pool_->GetTileSize(), 256u);
    EXPECT_EQ(pool_->GetMaxLayers(), 64u);
    EXPECT_EQ(pool_->GetFreeLayers(), 64u);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 0u);
}

TEST_F(TileTexturePoolTest, TextureArrayID_ZeroWhenSkippingGL) {
    EXPECT_EQ(pool_->GetTextureArrayID(), 0u);
}

// ============================================================================
// Upload Tests
// ============================================================================

TEST_F(TileTexturePoolTest, UploadSingleTile) {
    TileCoordinates tile(0, 0, 5);
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    int layer = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);

    EXPECT_GE(layer, 0);
    EXPECT_LT(layer, 64);
    EXPECT_EQ(pool_->GetFreeLayers(), 63u);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 1u);
    EXPECT_TRUE(pool_->IsTileLoaded(tile));
}

TEST_F(TileTexturePoolTest, UploadMultipleTiles) {
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    for (int i = 0; i < 10; ++i) {
        TileCoordinates tile(i, i, 5);
        int layer = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);
        EXPECT_GE(layer, 0);
    }

    EXPECT_EQ(pool_->GetFreeLayers(), 54u);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 10u);
}

TEST_F(TileTexturePoolTest, UploadAllLayers) {
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    for (int i = 0; i < 64; ++i) {
        TileCoordinates tile(i, i, 5);
        int layer = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);
        EXPECT_GE(layer, 0);
    }

    EXPECT_EQ(pool_->GetFreeLayers(), 0u);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 64u);
}

TEST_F(TileTexturePoolTest, DuplicateUpload_ReusesLayer) {
    TileCoordinates tile(5, 10, 8);
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    int layer1 = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);
    int layer2 = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);

    EXPECT_EQ(layer1, layer2);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 1u);
}

TEST_F(TileTexturePoolTest, UploadRejectsNullData) {
    TileCoordinates tile(0, 0, 5);
    int layer = pool_->UploadTile(tile, nullptr, 256, 256, 3);
    EXPECT_EQ(layer, -1);
}

TEST_F(TileTexturePoolTest, UploadRejectsMismatchedSize) {
    TileCoordinates tile(0, 0, 5);
    auto pixel_data = CreateTestPixelData(128, 128, 4);
    int layer = pool_->UploadTile(tile, pixel_data.data(), 128, 128, 4);
    EXPECT_EQ(layer, -1);
}

TEST_F(TileTexturePoolTest, UploadRejectsWrongChannelCount) {
    TileCoordinates tile(0, 0, 5);
    auto pixel_data = CreateTestPixelData(256, 256, 3);
    int layer = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
    EXPECT_EQ(layer, -1);
}

// ============================================================================
// Layer Index Lookup Tests
// ============================================================================

TEST_F(TileTexturePoolTest, GetLayerIndex_ExistingTile) {
    TileCoordinates tile(3, 7, 9);
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    int uploaded_layer = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);
    int lookup_layer = pool_->GetLayerIndex(tile);

    EXPECT_EQ(uploaded_layer, lookup_layer);
}

TEST_F(TileTexturePoolTest, GetLayerIndex_NonExistentTile) {
    TileCoordinates tile(99, 99, 10);
    int layer = pool_->GetLayerIndex(tile);
    EXPECT_EQ(layer, -1);
}

// ============================================================================
// Eviction Tests
// ============================================================================

TEST_F(TileTexturePoolTest, EvictTile_FreesLayer) {
    TileCoordinates tile(5, 5, 5);
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);
    ASSERT_EQ(pool_->GetOccupiedLayers(), 1u);

    pool_->EvictTile(tile);

    EXPECT_EQ(pool_->GetOccupiedLayers(), 0u);
    EXPECT_EQ(pool_->GetFreeLayers(), 64u);
    EXPECT_FALSE(pool_->IsTileLoaded(tile));
    EXPECT_EQ(pool_->GetLayerIndex(tile), -1);
}

TEST_F(TileTexturePoolTest, EvictNonExistentTile_NoOp) {
    TileCoordinates tile(99, 99, 10);
    pool_->EvictTile(tile);  // Should not crash
    EXPECT_EQ(pool_->GetOccupiedLayers(), 0u);
}

TEST_F(TileTexturePoolTest, UploadWhenFull_ReturnsFailure) {
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    // Fill all 64 layers
    for (int i = 0; i < 64; ++i) {
        TileCoordinates tile(i, i, 5);
        int layer = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);
        EXPECT_GE(layer, 0);
    }

    EXPECT_EQ(pool_->GetFreeLayers(), 0u);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 64u);

    // Upload one more — pool should refuse (no silent eviction)
    TileCoordinates new_tile(100, 100, 5);
    int layer = pool_->UploadTile(new_tile, pixel_data.data(), 256, 256, 4);

    EXPECT_EQ(layer, -1);
    EXPECT_FALSE(pool_->IsTileLoaded(new_tile));
    EXPECT_EQ(pool_->GetOccupiedLayers(), 64u);
}

TEST_F(TileTexturePoolTest, GetEvictionCandidate_ReturnsLRUTile) {
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    // Upload 3 tiles with time gaps
    TileCoordinates tile_a(0, 0, 5);
    TileCoordinates tile_b(1, 1, 5);
    TileCoordinates tile_c(2, 2, 5);

    pool_->UploadTile(tile_a, pixel_data.data(), 256, 256, 4);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    pool_->UploadTile(tile_b, pixel_data.data(), 256, 256, 4);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    pool_->UploadTile(tile_c, pixel_data.data(), 256, 256, 4);

    // LRU candidate should be tile_a (oldest)
    auto candidate = pool_->GetEvictionCandidate();
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->x, tile_a.x);
    EXPECT_EQ(candidate->y, tile_a.y);
    EXPECT_EQ(candidate->zoom, tile_a.zoom);
}

TEST_F(TileTexturePoolTest, GetEvictionCandidate_RespectsTouch) {
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    TileCoordinates tile_a(0, 0, 5);
    TileCoordinates tile_b(1, 1, 5);

    pool_->UploadTile(tile_a, pixel_data.data(), 256, 256, 4);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    pool_->UploadTile(tile_b, pixel_data.data(), 256, 256, 4);
    std::this_thread::sleep_for(std::chrono::microseconds(100));

    // Touch tile_a — now tile_b is oldest
    pool_->TouchTile(tile_a);

    auto candidate = pool_->GetEvictionCandidate();
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->x, tile_b.x);
    EXPECT_EQ(candidate->y, tile_b.y);
}

TEST_F(TileTexturePoolTest, GetEvictionCandidate_EmptyPool) {
    auto candidate = pool_->GetEvictionCandidate();
    EXPECT_FALSE(candidate.has_value());
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST_F(TileTexturePoolTest, ManyUploadsWithExplicitEviction) {
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    for (int i = 0; i < 200; ++i) {
        TileCoordinates tile(i, 0, 8);
        int layer = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);

        if (layer < 0) {
            // Pool full — evict explicitly, then retry
            auto candidate = pool_->GetEvictionCandidate();
            ASSERT_TRUE(candidate.has_value());
            pool_->EvictTile(*candidate);
            layer = pool_->UploadTile(tile, pixel_data.data(), 256, 256, 4);
            EXPECT_GE(layer, 0);
        }
    }

    EXPECT_EQ(pool_->GetOccupiedLayers(), 64u);
}

TEST_F(TileTexturePoolTest, EvictAndReuse) {
    // Use a pool with only 1 layer to guarantee reuse
    auto tiny_pool = std::make_unique<TileTexturePool>(256, 1, true);
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    TileCoordinates tile_a(0, 0, 5);
    TileCoordinates tile_b(1, 1, 5);

    int layer_a = tiny_pool->UploadTile(tile_a, pixel_data.data(), 256, 256, 4);
    tiny_pool->EvictTile(tile_a);

    int layer_b = tiny_pool->UploadTile(tile_b, pixel_data.data(), 256, 256, 4);

    EXPECT_EQ(layer_a, layer_b);
    EXPECT_FALSE(tiny_pool->IsTileLoaded(tile_a));
    EXPECT_TRUE(tiny_pool->IsTileLoaded(tile_b));
}

// ============================================================================
// Different Pool Sizes
// ============================================================================

TEST_F(TileTexturePoolTest, CustomPoolSize) {
    auto custom_pool = std::make_unique<TileTexturePool>(256, 128, true);

    EXPECT_EQ(custom_pool->GetMaxLayers(), 128u);
    EXPECT_EQ(custom_pool->GetFreeLayers(), 128u);
}

TEST_F(TileTexturePoolTest, SmallPool_FullReturnsFailure) {
    auto small_pool = std::make_unique<TileTexturePool>(256, 4, true);
    auto pixel_data = CreateTestPixelData(256, 256, 4);

    for (int i = 0; i < 4; ++i) {
        TileCoordinates tile(i, 0, 5);
        int layer = small_pool->UploadTile(tile, pixel_data.data(), 256, 256, 4);
        EXPECT_GE(layer, 0);
    }

    EXPECT_EQ(small_pool->GetFreeLayers(), 0u);

    // One more should fail (no silent eviction)
    TileCoordinates tile(10, 0, 5);
    int layer = small_pool->UploadTile(tile, pixel_data.data(), 256, 256, 4);
    EXPECT_EQ(layer, -1);
    EXPECT_EQ(small_pool->GetOccupiedLayers(), 4u);
}

// ============================================================================
// Integration: Pool + Indirection eviction coordination
// ============================================================================

TEST(TilePoolIndirectionIntegration, EvictionClearsIndirection) {
    // Simulates what the coordinator must do: when pool is full,
    // evict LRU tile and clear its indirection entry.
    auto pool = std::make_unique<TileTexturePool>(256, 4, true);
    IndirectionTextureManager indirection(true);

    auto pixel_data = std::vector<std::uint8_t>(256 * 256 * 4, 128);

    // Fill pool with 4 tiles at zoom 5
    for (int i = 0; i < 4; ++i) {
        TileCoordinates tile(i, 0, 5);
        int layer = pool->UploadTile(tile, pixel_data.data(), 256, 256, 4);
        ASSERT_GE(layer, 0);
        indirection.SetTileLayer(tile, static_cast<std::uint16_t>(layer));
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Verify all indirection entries are set
    for (int i = 0; i < 4; ++i) {
        EXPECT_NE(indirection.GetTileLayer(TileCoordinates(i, 0, 5)),
                  IndirectionTextureManager::kInvalidLayer);
    }

    // Pool is full — attempt to upload a 5th tile
    TileCoordinates new_tile(10, 0, 5);
    int layer = pool->UploadTile(new_tile, pixel_data.data(), 256, 256, 4);
    EXPECT_EQ(layer, -1);  // Pool refuses

    // Coordinator's job: evict LRU, clear indirection, retry
    auto candidate = pool->GetEvictionCandidate();
    ASSERT_TRUE(candidate.has_value());

    // Tile (0,0,5) should be the LRU candidate (uploaded first)
    EXPECT_EQ(candidate->x, 0);
    EXPECT_EQ(candidate->y, 0);
    EXPECT_EQ(candidate->zoom, 5);

    // Clear indirection BEFORE evicting from pool
    indirection.ClearTile(*candidate);
    pool->EvictTile(*candidate);

    // Verify evicted tile's indirection is cleared
    EXPECT_EQ(indirection.GetTileLayer(*candidate),
              IndirectionTextureManager::kInvalidLayer);

    // Retry upload — should succeed
    layer = pool->UploadTile(new_tile, pixel_data.data(), 256, 256, 4);
    EXPECT_GE(layer, 0);

    // Set indirection for new tile
    indirection.SetTileLayer(new_tile, static_cast<std::uint16_t>(layer));
    EXPECT_NE(indirection.GetTileLayer(new_tile),
              IndirectionTextureManager::kInvalidLayer);

    // Old tile (0,0,5) is gone from both pool and indirection
    EXPECT_FALSE(pool->IsTileLoaded(*candidate));
    EXPECT_EQ(indirection.GetTileLayer(*candidate),
              IndirectionTextureManager::kInvalidLayer);

    // Other tiles still intact
    for (int i = 1; i < 4; ++i) {
        TileCoordinates tile(i, 0, 5);
        EXPECT_TRUE(pool->IsTileLoaded(tile));
        EXPECT_NE(indirection.GetTileLayer(tile),
                  IndirectionTextureManager::kInvalidLayer);
    }
}

} // namespace earth_map::tests
