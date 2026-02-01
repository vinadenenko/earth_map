// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/data/elevation_data.h>

#include <algorithm>
#include <cmath>

namespace earth_map {

SRTMTileData::SRTMTileData(const SRTMMetadata& metadata)
    : metadata_(metadata), valid_(false) {
    // Allocate storage for elevation data
    const size_t total_samples = metadata_.samples_per_side * metadata_.samples_per_side;
    elevation_data_.resize(total_samples, 0);
}

ElevationSample SRTMTileData::GetSample(size_t x, size_t y) const noexcept {
    // Validate indices
    if (x >= metadata_.samples_per_side || y >= metadata_.samples_per_side) {
        return ElevationSample{};  // Invalid sample
    }

    // Row-major order: [y * width + x]
    const size_t index = y * metadata_.samples_per_side + x;
    const int16_t elevation = elevation_data_[index];

    return ElevationSample{elevation};
}

float SRTMTileData::InterpolateElevation(double lat_fraction,
                                         double lon_fraction) const noexcept {
    if (!valid_) {
        return 0.0f;
    }

    // Clamp fractions to valid range [0, 1)
    lat_fraction = std::clamp(lat_fraction, 0.0, 0.999999);
    lon_fraction = std::clamp(lon_fraction, 0.0, 0.999999);

    // Convert fractional coordinates to sample indices
    // Note: Y is inverted (row 0 is northernmost)
    const double x = lon_fraction * (metadata_.samples_per_side - 1);
    const double y = (1.0 - lat_fraction) * (metadata_.samples_per_side - 1);

    // Get integer and fractional parts
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));

    // Clamp indices to ensure x0+1 and y0+1 are within bounds
    const int max_index = static_cast<int>(metadata_.samples_per_side) - 2;
    x0 = std::clamp(x0, 0, max_index);
    y0 = std::clamp(y0, 0, max_index);

    const double dx = x - x0;
    const double dy = y - y0;

    // Get 4 corner samples
    const auto h00 = GetSample(x0, y0);
    const auto h10 = GetSample(x0 + 1, y0);
    const auto h01 = GetSample(x0, y0 + 1);
    const auto h11 = GetSample(x0 + 1, y0 + 1);

    // Handle voids - if any corner is invalid, return 0
    if (!h00.is_valid || !h10.is_valid || !h01.is_valid || !h11.is_valid) {
        return 0.0f;
    }

    // Bilinear interpolation
    const double h0 = h00.elevation_meters * (1.0 - dx) + h10.elevation_meters * dx;
    const double h1 = h01.elevation_meters * (1.0 - dx) + h11.elevation_meters * dx;
    const double interpolated = h0 * (1.0 - dy) + h1 * dy;

    return static_cast<float>(interpolated);
}

} // namespace earth_map
