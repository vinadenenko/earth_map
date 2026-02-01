// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#pragma once

#include "elevation_cache.h"
#include "elevation_data.h"
#include "srtm_loader.h"

#include <memory>
#include <vector>

#include <earth_map/coordinates/coordinate_spaces.h>

namespace earth_map {

/// Result of elevation query at a specific point
struct ElevationQuery {
    double latitude;                ///< Query latitude
    double longitude;               ///< Query longitude
    float elevation_meters;         ///< Elevation in meters above sea level
    bool valid;                     ///< True if elevation data is available
    SRTMCoordinates source_tile;   ///< SRTM tile that provided the data

    ElevationQuery() noexcept
        : latitude(0.0),
        longitude(0.0),
        elevation_meters(0.0f),
        valid(false),
        source_tile{0, 0} {}
};

/// High-level interface for querying elevation at any geographic point
/// Handles SRTM tile loading, caching, and interpolation automatically
class ElevationProvider {
public:
    virtual ~ElevationProvider() = default;

    /// Query elevation at a single geographic point
    /// @param latitude Latitude in degrees [-90, 90]
    /// @param longitude Longitude in degrees [-180, 180]
    /// @return Query result with elevation or error status
    [[nodiscard]] virtual ElevationQuery GetElevation(double latitude,
                                                      double longitude) const = 0;

    /// Query elevations at multiple points (batch operation)
    /// More efficient than calling GetElevation repeatedly
    /// @param points Vector of geographic coordinates to query
    /// @return Vector of query results (same order as input)
    [[nodiscard]] virtual std::vector<ElevationQuery> GetElevations(
        const std::vector<coordinates::Geographic>& points) const = 0;

    /// Preload SRTM tiles for a geographic region
    /// Useful for prefetching data before querying
    /// @param bounds Geographic bounds to preload
    /// @return Number of tiles successfully loaded
    virtual size_t PreloadRegion(const coordinates::GeographicBounds& bounds) = 0;

    /// Check if elevation data is available for a point
    /// Does not load tiles, only checks cache
    /// @param latitude Latitude in degrees
    /// @param longitude Longitude in degrees
    /// @return True if data is available (cached or loadable)
    [[nodiscard]] virtual bool IsAvailable(double latitude, double longitude) const = 0;

    /// Get current cache statistics
    /// @return Cache statistics
    [[nodiscard]] virtual ElevationCacheStats GetCacheStatistics() const = 0;

    /// Get current loader statistics
    /// @return Loader statistics
    [[nodiscard]] virtual SRTMLoaderStats GetLoaderStatistics() const = 0;

    /// Clear all cached elevation data
    virtual void ClearCache() = 0;

    /// Create elevation provider instance
    /// @param loader_config SRTM loader configuration
    /// @param cache_config Optional cache configuration
    /// @return Shared pointer to elevation provider
    [[nodiscard]] static std::shared_ptr<ElevationProvider> Create(
        const SRTMLoaderConfig& loader_config,
        const ElevationCacheConfig& cache_config = ElevationCacheConfig{});
};

/// Convert geographic coordinates to SRTM tile coordinates
/// @param latitude Latitude in degrees
/// @param longitude Longitude in degrees
/// @return SRTM tile coordinates (SW corner of 1° tile)
[[nodiscard]] inline SRTMCoordinates GeographicToSRTMTile(double latitude,
                                                          double longitude) noexcept {
    return {
        static_cast<int32_t>(std::floor(latitude)),
        static_cast<int32_t>(std::floor(longitude))
    };
}

/// Convert geographic coordinates to fractional position within SRTM tile
/// @param latitude Latitude in degrees
/// @param longitude Longitude in degrees
/// @param tile SRTM tile coordinates
/// @return Pair of (lat_fraction, lon_fraction) both in range [0, 1)
[[nodiscard]] inline std::pair<double, double> GeographicToTileFraction(
    double latitude, double longitude, const SRTMCoordinates& tile) noexcept {
    return {
        latitude - tile.latitude,    // [0, 1)
        longitude - tile.longitude   // [0, 1)
    };
}

/// Normalize longitude to range [-180, 180)
[[nodiscard]] inline double NormalizeLongitude(double longitude) noexcept {
    while (longitude >= 180.0) {
        longitude -= 360.0;
    }
    while (longitude < -180.0) {
        longitude += 360.0;
    }
    return longitude;
}

/// Normalize latitude to range [-90, 90]
[[nodiscard]] inline double NormalizeLatitude(double latitude) noexcept {
    return std::clamp(latitude, -90.0, 90.0);
}

} // namespace earth_map
