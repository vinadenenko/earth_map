#include <gtest/gtest.h>

#include <earth_map/renderer/tile_pool/indirection_texture_manager.h>

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
    std::string source_id = "page-table-test-imagery",
    std::string matrix_set_id = "WebMercatorQuad") {
    return {std::move(source_id), std::move(matrix_set_id), {level, column, row}};
}

}  // namespace

class IndirectionTextureManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_unique<IndirectionTextureManager>(true);
    }

    std::unique_ptr<IndirectionTextureManager> manager_;
};

TEST_F(IndirectionTextureManagerTest, InitiallyHasNoAllocatedPageTables) {
    EXPECT_TRUE(manager_->GetActiveZoomLevels().empty());
    EXPECT_EQ(manager_->GetTextureID(imagery::ImageTileKey{}), 0U);
}

TEST_F(IndirectionTextureManagerTest, MapsCanonicalPageInFullTable) {
    const auto key = MakeImageKey(3, 2, 4);

    EXPECT_TRUE(manager_->SetTileLayer(key, 42));
    EXPECT_EQ(manager_->GetTileLayer(key), 42U);
    EXPECT_EQ(manager_->GetWindowOffset(key).x, 0);
    EXPECT_EQ(manager_->GetWindowOffset(key).y, 0);
    EXPECT_EQ(manager_->GetActiveZoomLevels(), std::vector<int>({4}));

    const auto window = manager_->GetPageTableWindow(key);
    ASSERT_TRUE(window.has_value());
    EXPECT_TRUE(window->Matches(key));
    EXPECT_EQ(window->imagery_source_id, "page-table-test-imagery");
    EXPECT_EQ(window->matrix_set_id, "WebMercatorQuad");
    EXPECT_EQ(window->generation, 1U);
}

TEST_F(IndirectionTextureManagerTest, KeepsSourcesWithSameAddressSeparate) {
    const auto first_source = MakeImageKey(3, 2, 4, "imagery-a");
    const auto second_source = MakeImageKey(3, 2, 4, "imagery-b");

    ASSERT_TRUE(manager_->SetTileLayer(first_source, 11));
    ASSERT_TRUE(manager_->SetTileLayer(second_source, 22));

    EXPECT_EQ(manager_->GetTileLayer(first_source), 11U);
    EXPECT_EQ(manager_->GetTileLayer(second_source), 22U);
}

TEST_F(IndirectionTextureManagerTest, KeepsMatrixSetsWithSameAddressSeparate) {
    const auto first_matrix = MakeImageKey(3, 2, 4, "imagery", "matrix-a");
    const auto second_matrix = MakeImageKey(3, 2, 4, "imagery", "matrix-b");

    ASSERT_TRUE(manager_->SetTileLayer(first_matrix, 11));
    ASSERT_TRUE(manager_->SetTileLayer(second_matrix, 22));

    EXPECT_EQ(manager_->GetTileLayer(first_matrix), 11U);
    EXPECT_EQ(manager_->GetTileLayer(second_matrix), 22U);
}

TEST_F(IndirectionTextureManagerTest, ClearsOnlySpecifiedCanonicalPage) {
    const auto first = MakeImageKey(3, 2, 4, "imagery-a");
    const auto second = MakeImageKey(3, 2, 4, "imagery-b");

    ASSERT_TRUE(manager_->SetTileLayer(first, 11));
    ASSERT_TRUE(manager_->SetTileLayer(second, 22));
    manager_->ClearTile(first);

    EXPECT_EQ(manager_->GetTileLayer(first), IndirectionTextureManager::kInvalidLayer);
    EXPECT_EQ(manager_->GetTileLayer(second), 22U);
}

TEST_F(IndirectionTextureManagerTest, RejectsWindowedPageOutsideOwnedWindow) {
    const auto center = MakeImageKey(1000, 1000, 13);
    const auto outside = MakeImageKey(100, 100, 13);
    manager_->UpdateWindowCenter(center);

    EXPECT_FALSE(manager_->SetTileLayer(outside, 42));
    EXPECT_EQ(manager_->GetTileLayer(outside), IndirectionTextureManager::kInvalidLayer);
}

TEST_F(IndirectionTextureManagerTest, WindowedPageUsesAndMovesSourceAwareWindow) {
    const auto key = MakeImageKey(16000, 12000, 15);
    manager_->UpdateWindowCenter(key);
    ASSERT_TRUE(manager_->SetTileLayer(key, 77));

    EXPECT_EQ(manager_->GetTileLayer(key), 77U);
    EXPECT_EQ(manager_->GetWindowOffset(key).x, 15744);
    EXPECT_EQ(manager_->GetWindowOffset(key).y, 11744);

    const auto first_window = manager_->GetPageTableWindow(key);
    ASSERT_TRUE(first_window.has_value());
    const auto nearby_center = MakeImageKey(16010, 12010, 15);
    manager_->UpdateWindowCenter(nearby_center);
    EXPECT_EQ(manager_->GetTileLayer(key), 77U);

    const auto shifted_window = manager_->GetPageTableWindow(key);
    ASSERT_TRUE(shifted_window.has_value());
    EXPECT_GT(shifted_window->generation, first_window->generation);
}

TEST_F(IndirectionTextureManagerTest, FarWindowMoveDropsOldEntries) {
    const auto key = MakeImageKey(16000, 12000, 15);
    manager_->UpdateWindowCenter(key);
    ASSERT_TRUE(manager_->SetTileLayer(key, 77));

    manager_->UpdateWindowCenter(MakeImageKey(1000, 1000, 15));
    EXPECT_EQ(manager_->GetTileLayer(key), IndirectionTextureManager::kInvalidLayer);
}

TEST_F(IndirectionTextureManagerTest, ReleasingPageTableRemovesOnlyMatchingIdentity) {
    const auto first = MakeImageKey(3, 2, 4, "imagery-a");
    const auto second = MakeImageKey(3, 2, 4, "imagery-b");
    ASSERT_TRUE(manager_->SetTileLayer(first, 11));
    ASSERT_TRUE(manager_->SetTileLayer(second, 22));

    manager_->ReleasePageTable(first);

    EXPECT_EQ(manager_->GetTileLayer(first), IndirectionTextureManager::kInvalidLayer);
    EXPECT_EQ(manager_->GetTileLayer(second), 22U);
}

}  // namespace earth_map::tests
