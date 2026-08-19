#include <gtest/gtest.h>

#include <earth_map/renderer/tile_pool/tile_texture_pool.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace earth_map::tests {
namespace {

imagery::ImageTileKey MakeImageKey(
    std::uint32_t column,
    std::uint32_t row,
    std::uint32_t level,
    std::string source_id = "pool-test-imagery") {
    return {std::move(source_id), "WebMercatorQuad", {level, column, row}};
}

std::vector<std::uint8_t> MakePixels() {
    return std::vector<std::uint8_t>(256U * 256U * 4U, 127U);
}

}  // namespace

class TileTexturePoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = std::make_unique<TileTexturePool>(256, 4, true);
        pixels_ = MakePixels();
    }

    std::unique_ptr<TileTexturePool> pool_;
    std::vector<std::uint8_t> pixels_;
};

TEST_F(TileTexturePoolTest, InitializesWithAllLayersFree) {
    EXPECT_EQ(pool_->GetTileSize(), 256U);
    EXPECT_EQ(pool_->GetMaxLayers(), 4U);
    EXPECT_EQ(pool_->GetFreeLayers(), 4U);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 0U);
    EXPECT_EQ(pool_->GetTextureArrayID(), 0U);
}

TEST_F(TileTexturePoolTest, UploadsAndFindsCanonicalImageryPage) {
    const auto key = MakeImageKey(3, 7, 9);

    const int layer = pool_->UploadTile(key, pixels_.data(), 256, 256, 4);

    EXPECT_GE(layer, 0);
    EXPECT_TRUE(pool_->IsTileLoaded(key));
    EXPECT_EQ(pool_->GetLayerIndex(key), layer);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 1U);
}

TEST_F(TileTexturePoolTest, SameAddressFromDifferentSourcesUsesSeparateLayers) {
    const auto first_source = MakeImageKey(3, 7, 9, "imagery-a");
    const auto second_source = MakeImageKey(3, 7, 9, "imagery-b");

    const int first_layer = pool_->UploadTile(first_source, pixels_.data(), 256, 256, 4);
    const int second_layer = pool_->UploadTile(second_source, pixels_.data(), 256, 256, 4);

    EXPECT_GE(first_layer, 0);
    EXPECT_GE(second_layer, 0);
    EXPECT_NE(first_layer, second_layer);
    EXPECT_TRUE(pool_->IsTileLoaded(first_source));
    EXPECT_TRUE(pool_->IsTileLoaded(second_source));
    EXPECT_EQ(pool_->GetOccupiedLayers(), 2U);
}

TEST_F(TileTexturePoolTest, DuplicateCanonicalKeyUpdatesExistingLayer) {
    const auto key = MakeImageKey(5, 10, 8);

    const int first_layer = pool_->UploadTile(key, pixels_.data(), 256, 256, 4);
    const int second_layer = pool_->UploadTile(key, pixels_.data(), 256, 256, 4);

    EXPECT_EQ(first_layer, second_layer);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 1U);
}

TEST_F(TileTexturePoolTest, RejectsInvalidKeyAndInvalidPixelLayout) {
    const imagery::ImageTileKey invalid_key{};
    const auto valid_key = MakeImageKey(0, 0, 5);

    EXPECT_EQ(pool_->UploadTile(invalid_key, pixels_.data(), 256, 256, 4), -1);
    EXPECT_EQ(pool_->UploadTile(valid_key, nullptr, 256, 256, 4), -1);
    EXPECT_EQ(pool_->UploadTile(valid_key, pixels_.data(), 128, 128, 4), -1);
    EXPECT_EQ(pool_->UploadTile(valid_key, pixels_.data(), 256, 256, 3), -1);
    EXPECT_EQ(pool_->GetOccupiedLayers(), 0U);
}

TEST_F(TileTexturePoolTest, RequiresExplicitEvictionWhenFull) {
    for (std::uint32_t index = 0; index < 4; ++index) {
        EXPECT_GE(pool_->UploadTile(MakeImageKey(index, index, 5), pixels_.data(), 256, 256, 4), 0);
    }

    const auto extra_key = MakeImageKey(10, 10, 5);
    EXPECT_EQ(pool_->UploadTile(extra_key, pixels_.data(), 256, 256, 4), -1);
    EXPECT_FALSE(pool_->IsTileLoaded(extra_key));
}

TEST_F(TileTexturePoolTest, EvictionCandidateUsesCanonicalKeyAndLRUOrder) {
    const auto first = MakeImageKey(0, 0, 5);
    const auto second = MakeImageKey(1, 1, 5);
    const auto third = MakeImageKey(2, 2, 5);

    ASSERT_GE(pool_->UploadTile(first, pixels_.data(), 256, 256, 4), 0);
    ASSERT_GE(pool_->UploadTile(second, pixels_.data(), 256, 256, 4), 0);
    ASSERT_GE(pool_->UploadTile(third, pixels_.data(), 256, 256, 4), 0);
    pool_->TouchTile(first);

    const auto candidate = pool_->GetEvictionCandidate();
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(*candidate, second);
}

TEST_F(TileTexturePoolTest, EvictingCanonicalPageReleasesItsLayer) {
    const auto first = MakeImageKey(0, 0, 5);
    const auto second = MakeImageKey(1, 1, 5);
    auto one_layer_pool = std::make_unique<TileTexturePool>(256, 1, true);

    const int first_layer = one_layer_pool->UploadTile(first, pixels_.data(), 256, 256, 4);
    one_layer_pool->EvictTile(first);
    const int second_layer = one_layer_pool->UploadTile(second, pixels_.data(), 256, 256, 4);

    EXPECT_EQ(first_layer, second_layer);
    EXPECT_FALSE(one_layer_pool->IsTileLoaded(first));
    EXPECT_TRUE(one_layer_pool->IsTileLoaded(second));
}

}  // namespace earth_map::tests
