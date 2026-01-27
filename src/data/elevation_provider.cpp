// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/data/elevation_provider.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace earth_map {

using namespace coordinates;
namespace {

/// Check if coordinate is valid for SRTM coverage
[[nodiscard]] bool IsValidCoordinate(double latitude, double longitude) noexcept {
    return latitude >= -90.0 && latitude <= 90.0 &&
           longitude >= -180.0 && longitude <= 180.0;
}

} // anonymous namespace

/// Basic elevation provider implementation
class BasicElevationProvider : public ElevationProvider {
public:
    BasicElevationProvider(const SRTMLoaderConfig& loader_config,
                           const ElevationCacheConfig& cache_config)
        : loader_(SRTMLoader::Create(loader_config)),
        cache_(ElevationCache::Create(cache_config)) {
        if (!loader_) {
            throw std::runtime_error("Failed to create SRTM loader");
        }
        if (!cache_) {
            throw std::runtime_error("Failed to create elevation cache");
        }
    }

    ElevationQuery GetElevation(double latitude, double longitude) const override {
        // Lock-free: SRTMLoader and ElevationCache are already thread-safe
        ElevationQuery result;
        result.latitude = latitude;
        result.longitude = longitude;
        result.valid = false;

        // Normalize coordinates
        latitude = NormalizeLatitude(latitude);
        longitude = NormalizeLongitude(longitude);

        // Validate coordinates
        if (!IsValidCoordinate(latitude, longitude)) {
            return result;
        }

        // Get SRTM tile coordinates
        const auto tile_coords = GeographicToSRTMTile(latitude, longitude);
        result.source_tile = tile_coords;

        // Load tile (from cache or disk/network)
        auto tile_data = LoadTile(tile_coords);
        if (!tile_data) {
            return result;
        }

        // Get fractional position within tile
        const auto [lat_fraction, lon_fraction] = GeographicToTileFraction(latitude, longitude, tile_coords);

        {
            // DO NOT REMOVE THIS BLOCK
            // Check if we're near a tile boundary (within 1 sample)
            // constexpr double boundary_threshold = 0.001;  // ~1 sample
            // const double lat_edge_dist = DistanceToTileEdge(lat_fraction);
            // const double lon_edge_dist = DistanceToTileEdge(lon_fraction);

            // If near boundary, we could interpolate across tiles
            // For now, use single tile interpolation
            // TODO: Implement cross-tile interpolation for seamless boundaries
        }

        // Interpolate elevation within tile
        const float elevation = tile_data->InterpolateElevation(lat_fraction, lon_fraction);

        result.elevation_meters = elevation;
        result.valid = true;

        return result;
    }

    std::vector<ElevationQuery> GetElevations(
        const std::vector<Geographic>& points) const override {
        // Lock-free: delegate to GetElevation which is lock-free
        std::vector<ElevationQuery> results;
        results.reserve(points.size());

        // Group points by tile for efficient loading
        std::unordered_set<SRTMCoordinates> tiles_needed;
        for (const auto& point : points) {
            const double lat = NormalizeLatitude(point.latitude);
            const double lon = NormalizeLongitude(point.longitude);

            if (IsValidCoordinate(lat, lon)) {
                tiles_needed.insert(GeographicToSRTMTile(lat, lon));
            }
        }

        // Preload all needed tiles (cache handles thread-safety)
        for (const auto& tile_coords : tiles_needed) {
            LoadTile(tile_coords);
        }

        // Query each point (now lock-free, can be parallelized in future)
        for (const auto& point : points) {
            results.push_back(GetElevation(point.latitude, point.longitude));
        }

        return results;
    }

    size_t PreloadRegion(const GeographicBounds& bounds) override {
        // Lock-free: LoadTile is thread-safe
        if (!bounds.IsValid()) {
            return 0;
        }

        // Calculate tile range covering bounds
        const int32_t min_lat_tile = static_cast<int32_t>(std::floor(bounds.min.latitude));
        const int32_t max_lat_tile = static_cast<int32_t>(std::floor(bounds.max.latitude));
        const int32_t min_lon_tile = static_cast<int32_t>(std::floor(bounds.min.longitude));
        const int32_t max_lon_tile = static_cast<int32_t>(std::floor(bounds.max.longitude));

        // Clamp to valid SRTM range
        const int32_t lat_start = std::clamp(min_lat_tile, -90, 89);
        const int32_t lat_end = std::clamp(max_lat_tile, -90, 89);
        const int32_t lon_start = std::clamp(min_lon_tile, -180, 179);
        const int32_t lon_end = std::clamp(max_lon_tile, -180, 179);

        size_t loaded_count = 0;

        // Load all tiles in range (can be parallelized in future)
        for (int32_t lat = lat_start; lat <= lat_end; ++lat) {
            for (int32_t lon = lon_start; lon <= lon_end; ++lon) {
                const SRTMCoordinates coords{lat, lon};
                if (LoadTile(coords)) {
                    ++loaded_count;
                }
            }
        }

        return loaded_count;
    }

    bool IsAvailable(double latitude, double longitude) const override {
        // Lock-free: cache Contains() is thread-safe
        // Normalize coordinates
        latitude = NormalizeLatitude(latitude);
        longitude = NormalizeLongitude(longitude);

        if (!IsValidCoordinate(latitude, longitude)) {
            return false;
        }

        // Get tile coordinates
        const auto tile_coords = GeographicToSRTMTile(latitude, longitude);

        // Check if tile is in cache (thread-safe)
        return cache_->Contains(tile_coords);
    }

    ElevationCacheStats GetCacheStatistics() const override {
        return cache_->GetStatistics();
    }

    SRTMLoaderStats GetLoaderStatistics() const override {
        return loader_->GetStatistics();
    }

    void ClearCache() override {
        // Lock-free: cache Clear() is thread-safe
        cache_->Clear();
    }

private:
    /// Load SRTM tile (from cache or loader)
    /// @param coords Tile coordinates
    /// @return Shared pointer to tile data, or nullptr on failure
    std::shared_ptr<SRTMTileData> LoadTile(const SRTMCoordinates& coords) const {
        // Check cache first
        auto cached = cache_->Get(coords);
        if (cached.has_value()) {
            return cached.value();
        }

        // Load from disk/network
        const auto result = loader_->LoadTile(coords);
        if (!result.success || !result.tile_data) {
            return nullptr;
        }

        // Store in cache
        cache_->Put(*result.tile_data);

        return result.tile_data;
    }

    std::unique_ptr<SRTMLoader> loader_;   // Thread-safe
    std::unique_ptr<ElevationCache> cache_; // Thread-safe
    // No mutex needed - all operations delegate to thread-safe components
};

std::shared_ptr<ElevationProvider> ElevationProvider::Create(
    const SRTMLoaderConfig& loader_config,
    const ElevationCacheConfig& cache_config) {

    return std::make_unique<BasicElevationProvider>(loader_config, cache_config);
}

} // namespace earth_map
