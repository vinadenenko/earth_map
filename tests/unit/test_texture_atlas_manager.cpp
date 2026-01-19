#include <gtest/gtest.h>
#include <earth_map/renderer/texture_atlas/texture_atlas_manager.h>
#include <earth_map/math/tile_mathematics.h>
#include <glm/glm.hpp>
#include <vector>

namespace earth_map::tests {

/**
 * @brief Test fixture for TextureAtlasManager
 *
 * Note: Some tests require OpenGL context and are disabled by default.
 * Tests focus on logical components (UV calculation, slot management, eviction).
 */
class TextureAtlasManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Note: We're testing the logic without actual GL calls
        // Full integration tests require GL context
        manager_ = std::make_unique<TextureAtlasManager>(
            2048,  // atlas_width
            2048,  // atlas_height
            256,   // tile_size
            false  // skip_gl_init (no GL calls in tests)
        );
    }

    void TearDown() override {
        manager_.reset();
    }

    /**
     * @brief Create test pixel data
     */
    std::vector<std::uint8_t> CreateTestPixelData(
        std::uint32_t width, std::uint32_t height, std::uint8_t channels) {

        const std::size_t size = width * height * channels;
        std::vector<std::uint8_t> data(size);

        // Fill with test pattern
        for (std::size_t i = 0; i < size; ++i) {
            data[i] = static_cast<std::uint8_t>(i % 256);
        }

        return data;
    }

    std::unique_ptr<TextureAtlasManager> manager_;
};

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(TextureAtlasManagerTest, Initialization) {
    EXPECT_EQ(manager_->GetAtlasWidth(), 2048u);
    EXPECT_EQ(manager_->GetAtlasHeight(), 2048u);
    EXPECT_EQ(manager_->GetTileSize(), 256u);
    EXPECT_EQ(manager_->GetTotalSlots(), 64u);  // 8x8 grid
    EXPECT_EQ(manager_->GetFreeSlots(), 64u);
    EXPECT_EQ(manager_->GetOccupiedSlots(), 0u);
}

TEST_F(TextureAtlasManagerTest, GridDimensions) {
    EXPECT_EQ(manager_->GetGridWidth(), 8u);
    EXPECT_EQ(manager_->GetGridHeight(), 8u);
}

// ============================================================================
// UV Coordinate Calculation Tests
// ============================================================================

TEST_F(TextureAtlasManagerTest, UVCalculation_TopLeftSlot) {
    // Slot 0 is top-left corner (0, 0)
    glm::vec4 uv = manager_->CalculateSlotUV(0);

    // UV coordinates: (u_min, v_min, u_max, v_max)
    EXPECT_FLOAT_EQ(uv.x, 0.0f);      // u_min
    EXPECT_FLOAT_EQ(uv.y, 0.0f);      // v_min
    EXPECT_FLOAT_EQ(uv.z, 0.125f);    // u_max (256/2048 = 1/8)
    EXPECT_FLOAT_EQ(uv.w, 0.125f);    // v_max
}

TEST_F(TextureAtlasManagerTest, UVCalculation_BottomRightSlot) {
    // Slot 63 is bottom-right corner (7, 7)
    glm::vec4 uv = manager_->CalculateSlotUV(63);

    EXPECT_FLOAT_EQ(uv.x, 0.875f);    // u_min (7/8)
    EXPECT_FLOAT_EQ(uv.y, 0.875f);    // v_min (7/8)
    EXPECT_FLOAT_EQ(uv.z, 1.0f);      // u_max
    EXPECT_FLOAT_EQ(uv.w, 1.0f);      // v_max
}

TEST_F(TextureAtlasManagerTest, UVCalculation_MiddleSlot) {
    // Slot 27 is at (3, 3) in the grid
    glm::vec4 uv = manager_->CalculateSlotUV(27);

    EXPECT_FLOAT_EQ(uv.x, 0.375f);    // u_min (3/8)
    EXPECT_FLOAT_EQ(uv.y, 0.375f);    // v_min (3/8)
    EXPECT_FLOAT_EQ(uv.z, 0.5f);      // u_max (4/8)
    EXPECT_FLOAT_EQ(uv.w, 0.5f);      // v_max (4/8)
}

TEST_F(TextureAtlasManagerTest, UVCalculation_AllSlots) {
    // Verify all 64 slots have valid, non-overlapping UV coords
    std::vector<glm::vec4> all_uvs;

    for (int slot = 0; slot < 64; ++slot) {
        glm::vec4 uv = manager_->CalculateSlotUV(slot);

        // Check UV bounds
        EXPECT_GE(uv.x, 0.0f);
        EXPECT_GE(uv.y, 0.0f);
        EXPECT_LE(uv.z, 1.0f);
        EXPECT_LE(uv.w, 1.0f);

        // Check that max > min
        EXPECT_GT(uv.z, uv.x);
        EXPECT_GT(uv.w, uv.y);

        // Check size consistency
        float u_size = uv.z - uv.x;
        float v_size = uv.w - uv.y;
        EXPECT_FLOAT_EQ(u_size, 0.125f);  // 1/8
        EXPECT_FLOAT_EQ(v_size, 0.125f);

        all_uvs.push_back(uv);
    }

    // Verify no overlaps (basic check - different UVs)
    for (std::size_t i = 0; i < all_uvs.size(); ++i) {
        for (std::size_t j = i + 1; j < all_uvs.size(); ++j) {
            bool different = (all_uvs[i].x != all_uvs[j].x) ||
                           (all_uvs[i].y != all_uvs[j].y);
            EXPECT_TRUE(different);
        }
    }
}

// ============================================================================
// Slot Allocation Tests
// ============================================================================

TEST_F(TextureAtlasManagerTest, AllocateSingleSlot) {
    TileCoordinates tile(0, 0, 5);
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    int slot = manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);

    EXPECT_GE(slot, 0);
    EXPECT_LT(slot, 64);
    EXPECT_EQ(manager_->GetFreeSlots(), 63u);
    EXPECT_EQ(manager_->GetOccupiedSlots(), 1u);
}

TEST_F(TextureAtlasManagerTest, AllocateMultipleSlots) {
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 10; ++i) {
        tiles.emplace_back(i, i, 5);
    }

    auto pixel_data = CreateTestPixelData(256, 256, 3);

    for (const auto& tile : tiles) {
        int slot = manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
        EXPECT_GE(slot, 0);
    }

    EXPECT_EQ(manager_->GetFreeSlots(), 54u);
    EXPECT_EQ(manager_->GetOccupiedSlots(), 10u);
}

TEST_F(TextureAtlasManagerTest, AllocateAllSlots) {
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    // Fill all 64 slots
    for (int i = 0; i < 64; ++i) {
        TileCoordinates tile(i, i, 5);
        int slot = manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
        EXPECT_GE(slot, 0);
    }

    EXPECT_EQ(manager_->GetFreeSlots(), 0u);
    EXPECT_EQ(manager_->GetOccupiedSlots(), 64u);
}

TEST_F(TextureAtlasManagerTest, AllocateWhenFull_TriggersEviction) {
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    // Fill all 64 slots
    for (int i = 0; i < 64; ++i) {
        TileCoordinates tile(i, i, 5);
        manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
    }

    // Try to upload one more - should evict LRU and succeed
    TileCoordinates new_tile(100, 100, 5);
    int slot = manager_->UploadTile(new_tile, pixel_data.data(), 256, 256, 3);

    EXPECT_GE(slot, 0);
    EXPECT_EQ(manager_->GetOccupiedSlots(), 64u);  // Still 64, one was evicted
}

TEST_F(TextureAtlasManagerTest, DuplicateUpload_ReusesSlot) {
    TileCoordinates tile(5, 10, 8);
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    int slot1 = manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
    EXPECT_GE(slot1, 0);

    // Upload same tile again
    int slot2 = manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);

    // Should return same slot (update in place)
    EXPECT_EQ(slot1, slot2);
    EXPECT_EQ(manager_->GetOccupiedSlots(), 1u);
}

// ============================================================================
// Tile Lookup Tests
// ============================================================================

TEST_F(TextureAtlasManagerTest, GetTileUV_ExistingTile) {
    TileCoordinates tile(3, 7, 9);
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    int slot = manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
    ASSERT_GE(slot, 0);

    glm::vec4 uv = manager_->GetTileUV(tile);

    // Should return valid UV (not default)
    EXPECT_GE(uv.x, 0.0f);
    EXPECT_GE(uv.y, 0.0f);
    EXPECT_GT(uv.z, uv.x);
    EXPECT_GT(uv.w, uv.y);
}

TEST_F(TextureAtlasManagerTest, GetTileUV_NonExistentTile) {
    TileCoordinates tile(99, 99, 10);

    glm::vec4 uv = manager_->GetTileUV(tile);

    // Should return default UV (0, 0, 0, 0) or some sentinel value
    // Implementation may vary - check that it's distinguishable
    bool is_default = (uv.x == 0.0f && uv.y == 0.0f && uv.z == 0.0f && uv.w == 0.0f);
    EXPECT_TRUE(is_default);
}

TEST_F(TextureAtlasManagerTest, IsTileLoaded_ReturnsTrueAfterUpload) {
    TileCoordinates tile(1, 2, 3);
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    EXPECT_FALSE(manager_->IsTileLoaded(tile));

    manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);

    EXPECT_TRUE(manager_->IsTileLoaded(tile));
}

// ============================================================================
// Eviction Tests (LRU)
// ============================================================================

TEST_F(TextureAtlasManagerTest, EvictTile_FreesSlot) {
    TileCoordinates tile(5, 5, 5);
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
    ASSERT_EQ(manager_->GetOccupiedSlots(), 1u);

    manager_->EvictTile(tile);

    EXPECT_EQ(manager_->GetOccupiedSlots(), 0u);
    EXPECT_EQ(manager_->GetFreeSlots(), 64u);
    EXPECT_FALSE(manager_->IsTileLoaded(tile));
}

TEST_F(TextureAtlasManagerTest, EvictMultipleTiles) {
    auto pixel_data = CreateTestPixelData(256, 256, 3);
    std::vector<TileCoordinates> tiles;

    for (int i = 0; i < 10; ++i) {
        TileCoordinates tile(i, i, 5);
        tiles.push_back(tile);
        manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
    }

    ASSERT_EQ(manager_->GetOccupiedSlots(), 10u);

    // Evict 5 tiles
    for (int i = 0; i < 5; ++i) {
        manager_->EvictTile(tiles[i]);
    }

    EXPECT_EQ(manager_->GetOccupiedSlots(), 5u);
    EXPECT_EQ(manager_->GetFreeSlots(), 59u);
}

TEST_F(TextureAtlasManagerTest, LRU_Eviction_EvictsOldestTile) {
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    // Fill all slots
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 64; ++i) {
        TileCoordinates tile(i, i, 5);
        tiles.push_back(tile);
        manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);

        // Small delay to ensure different timestamps
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Upload a new tile - should evict tile 0 (oldest)
    TileCoordinates new_tile(100, 100, 5);
    manager_->UploadTile(new_tile, pixel_data.data(), 256, 256, 3);

    // Tile 0 should be evicted
    EXPECT_FALSE(manager_->IsTileLoaded(tiles[0]));

    // New tile should be loaded
    EXPECT_TRUE(manager_->IsTileLoaded(new_tile));

    // Other tiles should still be loaded
    for (int i = 1; i < 64; ++i) {
        EXPECT_TRUE(manager_->IsTileLoaded(tiles[i]));
    }
}

TEST_F(TextureAtlasManagerTest, LRU_AccessUpdatesTimestamp) {
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    // Upload 64 tiles
    std::vector<TileCoordinates> tiles;
    for (int i = 0; i < 64; ++i) {
        TileCoordinates tile(i, i, 5);
        tiles.push_back(tile);
        manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Access tile 0 (should update its timestamp)
    manager_->GetTileUV(tiles[0]);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Upload new tile - should evict tile 1 (now oldest), not tile 0
    TileCoordinates new_tile(100, 100, 5);
    manager_->UploadTile(new_tile, pixel_data.data(), 256, 256, 3);

    // Tile 0 should still be loaded (was recently accessed)
    EXPECT_TRUE(manager_->IsTileLoaded(tiles[0]));

    // Tile 1 should be evicted
    EXPECT_FALSE(manager_->IsTileLoaded(tiles[1]));
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST_F(TextureAtlasManagerTest, ManyUploadsAndEvictions) {
    auto pixel_data = CreateTestPixelData(256, 256, 3);

    // Upload 200 tiles (more than atlas capacity)
    for (int i = 0; i < 200; ++i) {
        TileCoordinates tile(i % 128, i % 128, 8);
        int slot = manager_->UploadTile(tile, pixel_data.data(), 256, 256, 3);
        EXPECT_GE(slot, 0);
    }

    // Atlas should be full but stable
    EXPECT_EQ(manager_->GetOccupiedSlots(), 64u);
}

TEST_F(TextureAtlasManagerTest, GetAtlasTextureID_ReturnsValidID) {
    std::uint32_t atlas_id = manager_->GetAtlasTextureID();

    // Should return 0 when GL is disabled (skip_gl_init = true)
    // In real GL context, would be non-zero
    EXPECT_EQ(atlas_id, 0u);
}

} // namespace earth_map::tests
