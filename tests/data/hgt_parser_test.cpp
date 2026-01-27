// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/data/hgt_parser.h>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace earth_map {
namespace {

/// Create synthetic SRTM3 data (1201x1201 samples)
std::vector<uint8_t> CreateSyntheticSRTM3Data(int16_t base_elevation = 1000) {
    constexpr size_t samples = 1201;
    constexpr size_t total_samples = samples * samples;
    constexpr size_t file_size = total_samples * sizeof(int16_t);

    std::vector<uint8_t> data(file_size);

    for (size_t i = 0; i < total_samples; ++i) {
        // Create gradient elevation (increases with sample index)
        const int16_t elevation = base_elevation + static_cast<int16_t>(i % 100);

        // Convert to big-endian
        data[i * 2] = static_cast<uint8_t>((elevation >> 8) & 0xFF);
        data[i * 2 + 1] = static_cast<uint8_t>(elevation & 0xFF);
    }

    return data;
}

/// Create synthetic SRTM1 data (3601x3601 samples)
std::vector<uint8_t> CreateSyntheticSRTM1Data(int16_t base_elevation = 2000) {
    constexpr size_t samples = 3601;
    constexpr size_t total_samples = samples * samples;
    constexpr size_t file_size = total_samples * sizeof(int16_t);

    std::vector<uint8_t> data(file_size);

    for (size_t i = 0; i < total_samples; ++i) {
        const int16_t elevation = base_elevation + static_cast<int16_t>(i % 200);

        data[i * 2] = static_cast<uint8_t>((elevation >> 8) & 0xFF);
        data[i * 2 + 1] = static_cast<uint8_t>(elevation & 0xFF);
    }

    return data;
}

TEST(HGTParserTest, ValidateSRTM3Size) {
    const auto data = CreateSyntheticSRTM3Data();
    EXPECT_TRUE(HGTParser::Validate(data));
}

TEST(HGTParserTest, ValidateSRTM1Size) {
    const auto data = CreateSyntheticSRTM1Data();
    EXPECT_TRUE(HGTParser::Validate(data));
}

TEST(HGTParserTest, ValidateInvalidSize) {
    std::vector<uint8_t> data(1000);  // Invalid size
    EXPECT_FALSE(HGTParser::Validate(data));
}

TEST(HGTParserTest, DetectSRTM3Resolution) {
    constexpr size_t srtm3_size = 1201 * 1201 * 2;
    const auto resolution = HGTParser::DetectResolution(srtm3_size);
    ASSERT_TRUE(resolution.has_value());
    EXPECT_EQ(resolution.value(), SRTMResolution::SRTM3);
}

TEST(HGTParserTest, DetectSRTM1Resolution) {
    constexpr size_t srtm1_size = 3601 * 3601 * 2;
    const auto resolution = HGTParser::DetectResolution(srtm1_size);
    ASSERT_TRUE(resolution.has_value());
    EXPECT_EQ(resolution.value(), SRTMResolution::SRTM1);
}

TEST(HGTParserTest, DetectInvalidResolution) {
    const auto resolution = HGTParser::DetectResolution(1000);
    EXPECT_FALSE(resolution.has_value());
}

TEST(HGTParserTest, ParseFilenameValid) {
    const auto coords = HGTParser::ParseFilename("N37W122.hgt");
    ASSERT_TRUE(coords.has_value());
    EXPECT_EQ(coords->latitude, 37);
    EXPECT_EQ(coords->longitude, -122);
}

TEST(HGTParserTest, ParseFilenameValidLowercase) {
    const auto coords = HGTParser::ParseFilename("n37w122.hgt");
    ASSERT_TRUE(coords.has_value());
    EXPECT_EQ(coords->latitude, 37);
    EXPECT_EQ(coords->longitude, -122);
}

TEST(HGTParserTest, ParseFilenameSouthEast) {
    const auto coords = HGTParser::ParseFilename("S34E018.hgt");
    ASSERT_TRUE(coords.has_value());
    EXPECT_EQ(coords->latitude, -34);
    EXPECT_EQ(coords->longitude, 18);
}

TEST(HGTParserTest, ParseFilenameWithPath) {
    const auto coords = HGTParser::ParseFilename("/path/to/N27E086.hgt");
    ASSERT_TRUE(coords.has_value());
    EXPECT_EQ(coords->latitude, 27);
    EXPECT_EQ(coords->longitude, 86);
}

TEST(HGTParserTest, ParseFilenameInvalid) {
    EXPECT_FALSE(HGTParser::ParseFilename("invalid.hgt").has_value());
    EXPECT_FALSE(HGTParser::ParseFilename("N37.hgt").has_value());
    EXPECT_FALSE(HGTParser::ParseFilename("N37W122.txt").has_value());
    EXPECT_FALSE(HGTParser::ParseFilename("X37W122.hgt").has_value());
}

TEST(HGTParserTest, ParseSRTM3Data) {
    const auto data = CreateSyntheticSRTM3Data(1500);
    const SRTMCoordinates coords{37, -122};

    auto tile = HGTParser::Parse(data, coords);
    ASSERT_NE(tile, nullptr);
    EXPECT_TRUE(tile->IsValid());

    const auto& metadata = tile->GetMetadata();
    EXPECT_EQ(metadata.coordinates.latitude, 37);
    EXPECT_EQ(metadata.coordinates.longitude, -122);
    EXPECT_EQ(metadata.resolution, SRTMResolution::SRTM3);
    EXPECT_EQ(metadata.samples_per_side, 1201u);
}

TEST(HGTParserTest, ParseSRTM1Data) {
    const auto data = CreateSyntheticSRTM1Data(2500);
    const SRTMCoordinates coords{27, 86};

    auto tile = HGTParser::Parse(data, coords);
    ASSERT_NE(tile, nullptr);
    EXPECT_TRUE(tile->IsValid());

    const auto& metadata = tile->GetMetadata();
    EXPECT_EQ(metadata.coordinates.latitude, 27);
    EXPECT_EQ(metadata.coordinates.longitude, 86);
    EXPECT_EQ(metadata.resolution, SRTMResolution::SRTM1);
    EXPECT_EQ(metadata.samples_per_side, 3601u);
}

TEST(HGTParserTest, BigEndianConversion) {
    // Create minimal SRTM3 data with known elevation value
    std::vector<uint8_t> data = CreateSyntheticSRTM3Data();

    // Set first sample to known value: 8848 (Mt. Everest height)
    constexpr int16_t test_elevation = 8848;
    data[0] = static_cast<uint8_t>((test_elevation >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>(test_elevation & 0xFF);

    const SRTMCoordinates coords{27, 86};
    auto tile = HGTParser::Parse(data, coords);
    ASSERT_NE(tile, nullptr);

    // Get sample at (0, 0) which is the first sample
    const auto sample = tile->GetSample(0, 0);
    EXPECT_TRUE(sample.is_valid);
    EXPECT_EQ(sample.elevation_meters, test_elevation);
}

TEST(HGTParserTest, VoidDetection) {
    auto data = CreateSyntheticSRTM3Data();

    // Insert void value (-32768) at specific location
    constexpr int16_t void_value = -32768;
    constexpr size_t void_index = 100;

    data[void_index * 2] = static_cast<uint8_t>((void_value >> 8) & 0xFF);
    data[void_index * 2 + 1] = static_cast<uint8_t>(void_value & 0xFF);

    const SRTMCoordinates coords{0, 0};
    auto tile = HGTParser::Parse(data, coords);
    ASSERT_NE(tile, nullptr);

    // After void filling, the void should be filled
    // (or marked as invalid if no neighbors)
    EXPECT_TRUE(tile->GetMetadata().has_voids);
}

TEST(HGTParserTest, InvalidCoordinates) {
    const auto data = CreateSyntheticSRTM3Data();

    // Test with invalid latitude
    SRTMCoordinates invalid_coords{-91, 0};
    auto tile = HGTParser::Parse(data, invalid_coords);
    EXPECT_EQ(tile, nullptr);

    // Test with invalid longitude
    invalid_coords = SRTMCoordinates{0, 180};
    tile = HGTParser::Parse(data, invalid_coords);
    EXPECT_EQ(tile, nullptr);
}

TEST(HGTParserTest, SampleAccess) {
    const auto data = CreateSyntheticSRTM3Data(1000);
    const SRTMCoordinates coords{0, 0};

    auto tile = HGTParser::Parse(data, coords);
    ASSERT_NE(tile, nullptr);

    // Access corner samples
    const auto sample_00 = tile->GetSample(0, 0);
    EXPECT_TRUE(sample_00.is_valid);

    const auto sample_max = tile->GetSample(1200, 1200);
    EXPECT_TRUE(sample_max.is_valid);

    // Access out of bounds should return invalid sample
    const auto sample_oob = tile->GetSample(1201, 1201);
    EXPECT_FALSE(sample_oob.is_valid);
}

TEST(HGTParserTest, InterpolationBasic) {
    const auto data = CreateSyntheticSRTM3Data(1000);
    const SRTMCoordinates coords{0, 0};

    auto tile = HGTParser::Parse(data, coords);
    ASSERT_NE(tile, nullptr);

    // Interpolate at center of tile
    const float elevation = tile->InterpolateElevation(0.5, 0.5);
    EXPECT_GT(elevation, 0.0f);

    // Interpolate at corners (should match sample values)
    const float elevation_00 = tile->InterpolateElevation(0.0, 0.0);
    EXPECT_GT(elevation_00, 0.0f);

    const float elevation_11 = tile->InterpolateElevation(0.999, 0.999);
    EXPECT_GT(elevation_11, 0.0f);
}

} // anonymous namespace
} // namespace earth_map
