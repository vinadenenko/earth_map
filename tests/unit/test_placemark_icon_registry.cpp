#include <gtest/gtest.h>

#include <earth_map/placemarks/placemark_icon_registry.h>

namespace earth_map::placemarks::tests {
namespace {

std::vector<std::uint8_t> MakeSolidIcon(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                        std::uint8_t a = 255) {
    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(PlacemarkIconRegistry::kIconCellSize) *
                   PlacemarkIconRegistry::kIconCellSize * 4U);
    for (std::uint32_t i = 0; i < PlacemarkIconRegistry::kIconCellSize *
                                       PlacemarkIconRegistry::kIconCellSize;
         ++i) {
        pixels.insert(pixels.end(), {r, g, b, a});
    }
    return pixels;
}

}  // namespace

TEST(PlacemarkIconRegistryTest, RegisterAssignsAtlasSlotAndBumpsRevision) {
    const auto registry = PlacemarkIconRegistry::Create();
    EXPECT_EQ(registry->Snapshot().Revision(), 0U);

    ASSERT_TRUE(registry->Register("pin-red", PlacemarkIconRegistry::kIconCellSize,
                                    PlacemarkIconRegistry::kIconCellSize,
                                    MakeSolidIcon(255, 0, 0)));

    const PlacemarkIconSnapshot snapshot = registry->Snapshot();
    EXPECT_EQ(snapshot.Revision(), 1U);
    const auto entry = snapshot.Find("pin-red");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->atlas_slot, 0U);
}

TEST(PlacemarkIconRegistryTest, ReRegisteringSameKeyReusesSlotWithoutConsumingCapacity) {
    const auto registry = PlacemarkIconRegistry::Create();
    ASSERT_TRUE(registry->Register("pin", PlacemarkIconRegistry::kIconCellSize,
                                    PlacemarkIconRegistry::kIconCellSize,
                                    MakeSolidIcon(255, 0, 0)));
    const auto first_slot = registry->Snapshot().Find("pin")->atlas_slot;

    ASSERT_TRUE(registry->Register("pin", PlacemarkIconRegistry::kIconCellSize,
                                    PlacemarkIconRegistry::kIconCellSize,
                                    MakeSolidIcon(0, 255, 0)));
    const PlacemarkIconSnapshot snapshot = registry->Snapshot();
    EXPECT_EQ(snapshot.Revision(), 2U);
    EXPECT_EQ(snapshot.Find("pin")->atlas_slot, first_slot);
    EXPECT_EQ(snapshot.Icons().size(), 1U);
}

TEST(PlacemarkIconRegistryTest, RejectsMismatchedDimensionsOrBufferSize) {
    const auto registry = PlacemarkIconRegistry::Create();

    EXPECT_FALSE(registry->Register("bad-size", 32, 32, MakeSolidIcon(1, 2, 3)));
    EXPECT_FALSE(registry->Register(
        "bad-buffer", PlacemarkIconRegistry::kIconCellSize,
        PlacemarkIconRegistry::kIconCellSize, std::vector<std::uint8_t>{1, 2, 3}));
    EXPECT_FALSE(registry->Register("", PlacemarkIconRegistry::kIconCellSize,
                                     PlacemarkIconRegistry::kIconCellSize,
                                     MakeSolidIcon(1, 2, 3)));
    EXPECT_EQ(registry->Snapshot().Revision(), 0U);
}

TEST(PlacemarkIconRegistryTest, RejectsRegistrationPastCapacity) {
    const auto registry = PlacemarkIconRegistry::Create();
    for (std::uint32_t i = 0; i < PlacemarkIconRegistry::kMaxIconCount; ++i) {
        ASSERT_TRUE(registry->Register("icon-" + std::to_string(i),
                                        PlacemarkIconRegistry::kIconCellSize,
                                        PlacemarkIconRegistry::kIconCellSize,
                                        MakeSolidIcon(1, 2, 3)));
    }
    EXPECT_FALSE(registry->Register("one-too-many", PlacemarkIconRegistry::kIconCellSize,
                                     PlacemarkIconRegistry::kIconCellSize,
                                     MakeSolidIcon(1, 2, 3)));
    EXPECT_EQ(registry->Snapshot().Icons().size(), PlacemarkIconRegistry::kMaxIconCount);
}

TEST(PlacemarkIconRegistryTest, SnapshotIsImmutableAfterFurtherRegistration) {
    const auto registry = PlacemarkIconRegistry::Create();
    ASSERT_TRUE(registry->Register("first", PlacemarkIconRegistry::kIconCellSize,
                                    PlacemarkIconRegistry::kIconCellSize,
                                    MakeSolidIcon(1, 2, 3)));
    const PlacemarkIconSnapshot first_snapshot = registry->Snapshot();

    ASSERT_TRUE(registry->Register("second", PlacemarkIconRegistry::kIconCellSize,
                                    PlacemarkIconRegistry::kIconCellSize,
                                    MakeSolidIcon(4, 5, 6)));

    EXPECT_EQ(first_snapshot.Icons().size(), 1U);
    EXPECT_FALSE(first_snapshot.Find("second").has_value());
}

TEST(PlacemarkIconRegistryTest, FindReturnsNulloptForUnknownKey) {
    const auto registry = PlacemarkIconRegistry::Create();
    EXPECT_FALSE(registry->Snapshot().Find("missing").has_value());
}

}  // namespace earth_map::placemarks::tests
