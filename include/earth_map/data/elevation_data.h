// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace earth_map {

/// SRTM tile coordinates representing the southwest corner of a 1° × 1° tile
struct SRTMCoordinates {
    int32_t latitude;   ///< SW corner latitude [-90, 89]
    int32_t longitude;  ///< SW corner longitude [-180, 179]

    bool operator==(const SRTMCoordinates& other) const noexcept {
        return latitude == other.latitude && longitude == other.longitude;
    }

    bool operator!=(const SRTMCoordinates& other) const noexcept {
        return !(*this == other);
    }

    /// Validate coordinates are within valid SRTM range
    [[nodiscard]] bool IsValid() const noexcept {
        return latitude >= -90 && latitude <= 89 &&
               longitude >= -180 && longitude <= 179;
    }
};

/// SRTM resolution types
enum class SRTMResolution {
    SRTM1,  ///< 1 arc-second (~30m) - 3601×3601 samples
    SRTM3   ///< 3 arc-second (~90m) - 1201×1201 samples
};

/// Get samples per side for a given resolution
[[nodiscard]] constexpr size_t GetSamplesPerSide(SRTMResolution resolution) noexcept {
    switch (resolution) {
        case SRTMResolution::SRTM1:
            return 3601;
        case SRTMResolution::SRTM3:
            return 1201;
        default:
            return 0;
    }
}

/// Get expected file size for a given resolution
[[nodiscard]] constexpr size_t GetExpectedFileSize(SRTMResolution resolution) noexcept {
    const size_t samples = GetSamplesPerSide(resolution);
    return samples * samples * sizeof(int16_t);
}

/// SRTM tile metadata
struct SRTMMetadata {
    SRTMCoordinates coordinates;
    SRTMResolution resolution;
    size_t samples_per_side;
    size_t file_size;
    bool has_voids;

    SRTMMetadata() noexcept
        : coordinates{0, 0},
          resolution(SRTMResolution::SRTM3),
          samples_per_side(0),
          file_size(0),
          has_voids(false) {}

    SRTMMetadata(const SRTMCoordinates& coords, SRTMResolution res) noexcept
        : coordinates(coords),
          resolution(res),
          samples_per_side(GetSamplesPerSide(res)),
          file_size(GetExpectedFileSize(res)),
          has_voids(false) {}
};

/// Single elevation sample from SRTM data
struct ElevationSample {
    int16_t elevation_meters;  ///< Elevation in meters (-32768 = void/missing data)
    bool is_valid;              ///< True if sample has valid data

    ElevationSample() noexcept : elevation_meters(0), is_valid(false) {}

    explicit ElevationSample(int16_t elevation) noexcept
        : elevation_meters(elevation),
          is_valid(elevation != -32768) {}
};

/// SRTM tile data container
/// Holds elevation data for a single 1° × 1° tile in row-major order
class SRTMTileData {
public:
    /// Construct with metadata
    explicit SRTMTileData(const SRTMMetadata& metadata);

    /// Default destructor
    ~SRTMTileData() = default;

    // Non-copyable but movable
    SRTMTileData(const SRTMTileData&) = delete;
    SRTMTileData& operator=(const SRTMTileData&) = delete;
    SRTMTileData(SRTMTileData&&) noexcept = default;
    SRTMTileData& operator=(SRTMTileData&&) noexcept = default;

    /// Get elevation sample at specific sample indices
    /// @param x Longitude sample index [0, samples_per_side - 1]
    /// @param y Latitude sample index [0, samples_per_side - 1]
    /// @return Elevation sample (may be invalid if void)
    [[nodiscard]] ElevationSample GetSample(size_t x, size_t y) const noexcept;

    /// Bilinear interpolation of elevation within tile
    /// @param lat_fraction Latitude fraction within tile [0, 1)
    /// @param lon_fraction Longitude fraction within tile [0, 1)
    /// @return Interpolated elevation in meters, or 0.0f if invalid
    [[nodiscard]] float InterpolateElevation(double lat_fraction,
                                             double lon_fraction) const noexcept;

    /// Get raw elevation data (mutable for population during parsing)
    [[nodiscard]] std::vector<int16_t>& GetRawData() noexcept {
        return elevation_data_;
    }

    /// Get raw elevation data (const)
    [[nodiscard]] const std::vector<int16_t>& GetRawData() const noexcept {
        return elevation_data_;
    }

    /// Get metadata
    [[nodiscard]] const SRTMMetadata& GetMetadata() const noexcept {
        return metadata_;
    }

    /// Check if tile has valid data
    [[nodiscard]] bool IsValid() const noexcept { return valid_; }

    /// Set validity flag
    void SetValid(bool valid) noexcept { valid_ = valid; }

    /// Mark that tile has voids
    void SetHasVoids(bool has_voids) noexcept {
        metadata_.has_voids = has_voids;
    }

private:
    SRTMMetadata metadata_;
    std::vector<int16_t> elevation_data_;  ///< Row-major: [y * width + x]
    bool valid_;
};

} // namespace earth_map

// Hash function for SRTMCoordinates (for use in unordered_map)
namespace std {
template <>
struct hash<earth_map::SRTMCoordinates> {
    size_t operator()(const earth_map::SRTMCoordinates& coords) const noexcept {
        // Combine latitude and longitude hashes
        const size_t h1 = std::hash<int32_t>{}(coords.latitude);
        const size_t h2 = std::hash<int32_t>{}(coords.longitude);
        return h1 ^ (h2 << 1);
    }
};
} // namespace std
