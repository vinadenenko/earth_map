// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/data/hgt_parser.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace earth_map {

std::unique_ptr<SRTMTileData> HGTParser::Parse(
    const std::vector<uint8_t>& data,
    const SRTMCoordinates& coordinates) {

    // Validate coordinates
    if (!coordinates.IsValid()) {
        return nullptr;
    }

    // Validate data
    if (!Validate(data)) {
        return nullptr;
    }

    // Detect resolution
    const auto resolution = DetectResolution(data.size());
    if (!resolution.has_value()) {
        return nullptr;
    }

    // Create tile with metadata
    SRTMMetadata metadata(coordinates, resolution.value());
    auto tile = std::make_unique<SRTMTileData>(metadata);

    // Parse samples
    auto& raw_data = tile->GetRawData();
    const size_t num_samples = metadata.samples_per_side * metadata.samples_per_side;
    bool has_voids = false;

    for (size_t i = 0; i < num_samples; ++i) {
        const size_t byte_offset = i * 2;
        const int16_t elevation = ParseSample(&data[byte_offset]);
        raw_data[i] = elevation;

        if (IsVoid(elevation)) {
            has_voids = true;
        }
    }

    // Fill voids if present
    if (has_voids) {
        FillVoids(*tile);
        tile->SetHasVoids(true);
    }

    tile->SetValid(true);
    return tile;
}

std::unique_ptr<SRTMTileData> HGTParser::ParseFile(const std::string& file_path) {
    // Try to extract coordinates from filename
    auto coords = ParseFilename(file_path);
    if (!coords.has_value()) {
        // Filename doesn't match pattern - need coordinates provided separately
        return nullptr;
    }

    // Open file in binary mode
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return nullptr;
    }

    // Get file size
    const std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read entire file into buffer
    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        return nullptr;
    }

    // Parse data
    return Parse(buffer, coords.value());
}

bool HGTParser::Validate(const std::vector<uint8_t>& data) noexcept {
    // Check if size matches known SRTM formats
    const size_t size = data.size();
    return (size == SRTM1_FILE_SIZE || size == SRTM3_FILE_SIZE);
}

std::optional<SRTMResolution> HGTParser::DetectResolution(size_t file_size) noexcept {
    if (file_size == SRTM1_FILE_SIZE) {
        return SRTMResolution::SRTM1;
    } else if (file_size == SRTM3_FILE_SIZE) {
        return SRTMResolution::SRTM3;
    }
    return std::nullopt;
}

std::optional<SRTMCoordinates> HGTParser::ParseFilename(const std::string& filename) noexcept {
    // Extract just the filename from path
    const size_t last_slash = filename.find_last_of("/\\");
    const std::string basename = (last_slash != std::string::npos)
                                      ? filename.substr(last_slash + 1)
                                      : filename;

    // Expected format: N37W122.hgt or n37w122.HGT (case insensitive)
    // Minimum length: 7 characters (e.g., N0E0.hgt)
    if (basename.length() < 7) {
        return std::nullopt;
    }

    // Convert to uppercase for parsing
    std::string upper = basename;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    // Check extension
    if (upper.find(".HGT") == std::string::npos) {
        return std::nullopt;
    }

    // Parse latitude
    if (upper[0] != 'N' && upper[0] != 'S') {
        return std::nullopt;
    }

    const bool lat_north = (upper[0] == 'N');

    // Find longitude separator
    size_t lon_pos = std::string::npos;
    for (size_t i = 1; i < upper.length(); ++i) {
        if (upper[i] == 'E' || upper[i] == 'W') {
            lon_pos = i;
            break;
        }
    }

    if (lon_pos == std::string::npos || lon_pos == 1) {
        return std::nullopt;
    }

    // Extract latitude value
    const std::string lat_str = upper.substr(1, lon_pos - 1);
    int32_t latitude = 0;
    try {
        latitude = std::stoi(lat_str);
    } catch (...) {
        return std::nullopt;
    }

    if (!lat_north) {
        latitude = -latitude;
    }

    // Parse longitude
    const bool lon_east = (upper[lon_pos] == 'E');

    // Find end of longitude number (before .HGT)
    const size_t ext_pos = upper.find(".HGT");
    if (ext_pos == std::string::npos || ext_pos <= lon_pos + 1) {
        return std::nullopt;
    }

    const std::string lon_str = upper.substr(lon_pos + 1, ext_pos - lon_pos - 1);
    int32_t longitude = 0;
    try {
        longitude = std::stoi(lon_str);
    } catch (...) {
        return std::nullopt;
    }

    if (!lon_east) {
        longitude = -longitude;
    }

    // Validate range
    SRTMCoordinates coords{latitude, longitude};
    if (!coords.IsValid()) {
        return std::nullopt;
    }

    return coords;
}

int16_t HGTParser::ParseSample(const uint8_t* bytes) noexcept {
    // Big-endian to little-endian conversion
    // HGT format stores data as big-endian signed 16-bit integers
    const int16_t value = static_cast<int16_t>((bytes[0] << 8) | bytes[1]);
    return value;
}

void HGTParser::FillVoids(SRTMTileData& tile) {
    auto& data = tile.GetRawData();
    const size_t size = tile.GetMetadata().samples_per_side;

    // Simple void filling: interpolate from valid neighbors
    // This is a basic implementation - could be improved with more sophisticated algorithms
    bool changed = true;
    size_t iterations = 0;
    const size_t max_iterations = MAX_VOID_FILL_GAP;

    while (changed && iterations < max_iterations) {
        changed = false;
        ++iterations;

        for (size_t y = 0; y < size; ++y) {
            for (size_t x = 0; x < size; ++x) {
                const size_t index = y * size + x;

                if (IsVoid(data[index])) {
                    const int16_t interpolated = InterpolateVoid(tile, x, y);
                    if (interpolated != VOID_VALUE) {
                        data[index] = interpolated;
                        changed = true;
                    }
                }
            }
        }
    }
}

int16_t HGTParser::InterpolateVoid(const SRTMTileData& tile, size_t x, size_t y) noexcept {
    const auto& data = tile.GetRawData();
    const size_t size = tile.GetMetadata().samples_per_side;

    // Collect valid neighbors
    int32_t sum = 0;
    size_t count = 0;

    // Check 4-connected neighbors (up, down, left, right)
    const int dx[] = {0, 0, -1, 1};
    const int dy[] = {-1, 1, 0, 0};

    for (size_t i = 0; i < 4; ++i) {
        const int nx = static_cast<int>(x) + dx[i];
        const int ny = static_cast<int>(y) + dy[i];

        if (nx >= 0 && nx < static_cast<int>(size) &&
            ny >= 0 && ny < static_cast<int>(size)) {
            const size_t neighbor_index = ny * size + nx;
            const int16_t neighbor_value = data[neighbor_index];

            if (!IsVoid(neighbor_value)) {
                sum += neighbor_value;
                ++count;
            }
        }
    }

    // If we have valid neighbors, return average
    if (count > 0) {
        return static_cast<int16_t>(sum / static_cast<int32_t>(count));
    }

    return VOID_VALUE;
}

} // namespace earth_map
