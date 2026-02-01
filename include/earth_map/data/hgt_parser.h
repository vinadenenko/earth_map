// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#pragma once

#include "elevation_data.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace earth_map {

/// HGT file parser for SRTM elevation data
/// Parses big-endian signed 16-bit integer elevation data
class HGTParser {
public:
    /// Parse SRTM data from memory buffer
    /// @param data Raw HGT file data
    /// @param coordinates SRTM tile coordinates
    /// @return Parsed tile data, or nullptr on error
    [[nodiscard]] static std::unique_ptr<SRTMTileData> Parse(
        const std::vector<uint8_t>& data,
        const SRTMCoordinates& coordinates);

    /// Parse SRTM data from disk file
    /// @param file_path Path to .hgt file
    /// @return Parsed tile data, or nullptr on error
    [[nodiscard]] static std::unique_ptr<SRTMTileData> ParseFile(
        const std::string& file_path);

    /// Validate HGT data format
    /// @param data Raw HGT file data
    /// @return True if data appears to be valid HGT format
    [[nodiscard]] static bool Validate(const std::vector<uint8_t>& data) noexcept;

    /// Detect SRTM resolution from file size
    /// @param file_size Size of HGT file in bytes
    /// @return Detected resolution, or nullopt if unrecognized
    [[nodiscard]] static std::optional<SRTMResolution> DetectResolution(
        size_t file_size) noexcept;

    /// Extract SRTM coordinates from filename (e.g., "N37W122.hgt")
    /// @param filename Name of HGT file
    /// @return Coordinates if filename is valid, nullopt otherwise
    [[nodiscard]] static std::optional<SRTMCoordinates> ParseFilename(
        const std::string& filename) noexcept;

private:
    /// File size constants
    static constexpr size_t SRTM1_FILE_SIZE = 3601 * 3601 * 2;  // 25,934,402 bytes
    static constexpr size_t SRTM3_FILE_SIZE = 1201 * 1201 * 2;  // 2,884,802 bytes

    /// Void indicator value
    static constexpr int16_t VOID_VALUE = -32768;

    /// Maximum gap size for void filling (samples)
    static constexpr size_t MAX_VOID_FILL_GAP = 10;

    /// Parse single elevation sample from big-endian bytes
    /// @param bytes Pointer to 2 bytes (big-endian)
    /// @return Elevation in meters
    [[nodiscard]] static int16_t ParseSample(const uint8_t* bytes) noexcept;

    /// Fill voids in tile data using interpolation
    /// @param tile Tile to process (modified in place)
    static void FillVoids(SRTMTileData& tile);

    /// Check if sample is a void
    [[nodiscard]] static bool IsVoid(int16_t sample) noexcept {
        return sample == VOID_VALUE;
    }

    /// Interpolate elevation for void sample from neighbors
    /// @param tile Tile data
    /// @param x X coordinate of void
    /// @param y Y coordinate of void
    /// @return Interpolated elevation, or VOID_VALUE if no valid neighbors
    [[nodiscard]] static int16_t InterpolateVoid(
        const SRTMTileData& tile, size_t x, size_t y) noexcept;
};

} // namespace earth_map
