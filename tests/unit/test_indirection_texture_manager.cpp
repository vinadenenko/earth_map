#include <gtest/gtest.h>
#include <earth_map/renderer/tile_pool/indirection_texture_manager.h>
#include <earth_map/math/tile_mathematics.h>

namespace earth_map::tests {

class IndirectionTextureManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_unique<IndirectionTextureManager>(true);  // skip GL
    }

    void TearDown() override {
        manager_.reset();
    }

    std::unique_ptr<IndirectionTextureManager> manager_;
};

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(IndirectionTextureManagerTest, InitiallyEmpty) {
    EXPECT_TRUE(manager_->GetActiveZoomLevels().empty());
}

TEST_F(IndirectionTextureManagerTest, TextureID_ZeroBeforeAllocation) {
    EXPECT_EQ(manager_->GetTextureID(5), 0u);
}

// ============================================================================
// Full Mode Tests (zoom 0-12)
// ============================================================================

TEST_F(IndirectionTextureManagerTest, SetTileLayer_CreatesZoomTexture) {
    TileCoordinates tile(3, 2, 4);
    manager_->SetTileLayer(tile, 42);

    auto active = manager_->GetActiveZoomLevels();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0], 4);
}

TEST_F(IndirectionTextureManagerTest, GetTileLayer_ReturnsSetValue) {
    TileCoordinates tile(3, 2, 4);
    manager_->SetTileLayer(tile, 42);

    std::uint16_t layer = manager_->GetTileLayer(tile);
    EXPECT_EQ(layer, 42u);
}

TEST_F(IndirectionTextureManagerTest, GetTileLayer_ReturnsInvalidForUnsetTile) {
    TileCoordinates tile(3, 2, 4);
    std::uint16_t layer = manager_->GetTileLayer(tile);
    EXPECT_EQ(layer, IndirectionTextureManager::kInvalidLayer);
}

TEST_F(IndirectionTextureManagerTest, ClearTile_ResetsToInvalid) {
    TileCoordinates tile(3, 2, 4);
    manager_->SetTileLayer(tile, 42);
    manager_->ClearTile(tile);

    std::uint16_t layer = manager_->GetTileLayer(tile);
    EXPECT_EQ(layer, IndirectionTextureManager::kInvalidLayer);
}

TEST_F(IndirectionTextureManagerTest, MultipleTilesInSameZoom) {
    TileCoordinates tile_a(0, 0, 2);
    TileCoordinates tile_b(1, 1, 2);
    TileCoordinates tile_c(3, 2, 2);

    manager_->SetTileLayer(tile_a, 10);
    manager_->SetTileLayer(tile_b, 20);
    manager_->SetTileLayer(tile_c, 30);

    EXPECT_EQ(manager_->GetTileLayer(tile_a), 10u);
    EXPECT_EQ(manager_->GetTileLayer(tile_b), 20u);
    EXPECT_EQ(manager_->GetTileLayer(tile_c), 30u);
}

TEST_F(IndirectionTextureManagerTest, MultipleZoomLevels) {
    TileCoordinates tile_z2(0, 0, 2);
    TileCoordinates tile_z5(10, 15, 5);
    TileCoordinates tile_z10(500, 300, 10);

    manager_->SetTileLayer(tile_z2, 1);
    manager_->SetTileLayer(tile_z5, 2);
    manager_->SetTileLayer(tile_z10, 3);

    auto active = manager_->GetActiveZoomLevels();
    EXPECT_EQ(active.size(), 3u);

    EXPECT_EQ(manager_->GetTileLayer(tile_z2), 1u);
    EXPECT_EQ(manager_->GetTileLayer(tile_z5), 2u);
    EXPECT_EQ(manager_->GetTileLayer(tile_z10), 3u);
}

TEST_F(IndirectionTextureManagerTest, OverwriteTileLayer) {
    TileCoordinates tile(3, 2, 4);
    manager_->SetTileLayer(tile, 42);
    manager_->SetTileLayer(tile, 99);

    EXPECT_EQ(manager_->GetTileLayer(tile), 99u);
}

TEST_F(IndirectionTextureManagerTest, ReleaseZoomLevel) {
    TileCoordinates tile(3, 2, 4);
    manager_->SetTileLayer(tile, 42);

    manager_->ReleaseZoomLevel(4);

    auto active = manager_->GetActiveZoomLevels();
    EXPECT_TRUE(active.empty());
    EXPECT_EQ(manager_->GetTileLayer(tile), IndirectionTextureManager::kInvalidLayer);
}

// ============================================================================
// Window Offset Tests (zoom 0-12: offset is always 0,0)
// ============================================================================

TEST_F(IndirectionTextureManagerTest, FullMode_OffsetIsZero) {
    TileCoordinates tile(3, 2, 4);
    manager_->SetTileLayer(tile, 42);

    auto offset = manager_->GetWindowOffset(4);
    EXPECT_EQ(offset.x, 0);
    EXPECT_EQ(offset.y, 0);
}

// ============================================================================
// Windowed Mode Tests (zoom 13+)
// ============================================================================

TEST_F(IndirectionTextureManagerTest, WindowedMode_SetAndGetTile) {
    // Zoom 15 is windowed mode
    TileCoordinates tile(16000, 12000, 15);
    manager_->UpdateWindowCenter(15, 16000, 12000);
    manager_->SetTileLayer(tile, 77);

    EXPECT_EQ(manager_->GetTileLayer(tile), 77u);
}

TEST_F(IndirectionTextureManagerTest, WindowedMode_OffsetNonZero) {
    manager_->UpdateWindowCenter(15, 16000, 12000);

    auto offset = manager_->GetWindowOffset(15);
    // Offset should center the window around (16000, 12000)
    // Window size is 512, so offset = center - 256
    EXPECT_EQ(offset.x, 16000 - 256);
    EXPECT_EQ(offset.y, 12000 - 256);
}

TEST_F(IndirectionTextureManagerTest, WindowedMode_TileOutsideWindow) {
    manager_->UpdateWindowCenter(15, 16000, 12000);

    // Tile far from window center
    TileCoordinates far_tile(0, 0, 15);
    manager_->SetTileLayer(far_tile, 50);

    // Should return invalid since tile is outside window
    EXPECT_EQ(manager_->GetTileLayer(far_tile), IndirectionTextureManager::kInvalidLayer);
}

TEST_F(IndirectionTextureManagerTest, WindowedMode_RecenterClearsOldData) {
    manager_->UpdateWindowCenter(15, 16000, 12000);

    TileCoordinates tile(16000, 12000, 15);
    manager_->SetTileLayer(tile, 77);
    ASSERT_EQ(manager_->GetTileLayer(tile), 77u);

    // Recenter far away — old tile should be cleared
    manager_->UpdateWindowCenter(15, 1000, 1000);
    EXPECT_EQ(manager_->GetTileLayer(tile), IndirectionTextureManager::kInvalidLayer);
}

TEST_F(IndirectionTextureManagerTest, WindowedMode_NearbyRecenterClearsAndRequiresReupload) {
    manager_->UpdateWindowCenter(15, 16000, 12000);

    TileCoordinates tile(16000, 12000, 15);
    manager_->SetTileLayer(tile, 77);

    // Move just 10 tiles — data is cleared (simple implementation)
    // Callers must re-upload tile layers after re-centering
    manager_->UpdateWindowCenter(15, 16010, 12010);
    EXPECT_EQ(manager_->GetTileLayer(tile), IndirectionTextureManager::kInvalidLayer);

    // After re-uploading, the tile is accessible again
    manager_->SetTileLayer(tile, 77);
    EXPECT_EQ(manager_->GetTileLayer(tile), 77u);
}

TEST_F(IndirectionTextureManagerTest, WindowedMode_MultipleZoomsCombine) {
    // One full, one windowed
    TileCoordinates full_tile(3, 2, 4);
    TileCoordinates windowed_tile(16000, 12000, 15);

    manager_->SetTileLayer(full_tile, 10);
    manager_->UpdateWindowCenter(15, 16000, 12000);
    manager_->SetTileLayer(windowed_tile, 20);

    EXPECT_EQ(manager_->GetTileLayer(full_tile), 10u);
    EXPECT_EQ(manager_->GetTileLayer(windowed_tile), 20u);

    auto active = manager_->GetActiveZoomLevels();
    EXPECT_EQ(active.size(), 2u);
}

// ============================================================================
// Boundary Tests
// ============================================================================

TEST_F(IndirectionTextureManagerTest, Zoom12_IsFullMode) {
    TileCoordinates tile(2048, 2048, 12);
    manager_->SetTileLayer(tile, 5);

    auto offset = manager_->GetWindowOffset(12);
    EXPECT_EQ(offset.x, 0);
    EXPECT_EQ(offset.y, 0);
    EXPECT_EQ(manager_->GetTileLayer(tile), 5u);
}

TEST_F(IndirectionTextureManagerTest, Zoom13_IsWindowedMode) {
    TileCoordinates tile(4096, 4096, 13);
    manager_->UpdateWindowCenter(13, 4096, 4096);
    manager_->SetTileLayer(tile, 5);

    auto offset = manager_->GetWindowOffset(13);
    // Should have non-zero offset
    EXPECT_NE(offset.x, 0);
    EXPECT_NE(offset.y, 0);
    EXPECT_EQ(manager_->GetTileLayer(tile), 5u);
}

TEST_F(IndirectionTextureManagerTest, ClearTile_NonExistentZoom_NoOp) {
    TileCoordinates tile(3, 2, 4);
    manager_->ClearTile(tile);  // Should not crash
}

TEST_F(IndirectionTextureManagerTest, WindowedMode_WindowSize) {
    EXPECT_EQ(IndirectionTextureManager::kWindowSize, 512u);
}

} // namespace earth_map::tests
