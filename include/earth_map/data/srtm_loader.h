// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#pragma once

#include "elevation_data.h"

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace earth_map {

/// SRTM data source types
enum class SRTMSource {
    LOCAL_DISK,  ///< Load from local disk directory
    HTTP         ///< Download from HTTP server
};

/// Result of SRTM tile load operation
struct SRTMLoadResult {
    bool success;                              ///< True if load succeeded
    std::shared_ptr<SRTMTileData> tile_data;  ///< Loaded tile data (nullptr on failure)
    SRTMCoordinates coordinates;              ///< Requested coordinates
    std::string error_message;                ///< Error description if failed
    double load_time_ms;                      ///< Time taken to load (milliseconds)
    size_t file_size_bytes;                   ///< Size of loaded data

    SRTMLoadResult()
        : success(false),
          tile_data(nullptr),
          coordinates{0, 0},
          error_message(),
          load_time_ms(0.0),
          file_size_bytes(0) {}
};

/// Callback for async SRTM tile loading
using SRTMLoadCallback = std::function<void(const SRTMLoadResult&)>;

/// Statistics for SRTM loader
struct SRTMLoaderStats {
    uint64_t tiles_loaded;         ///< Total tiles successfully loaded
    uint64_t tiles_failed;         ///< Total tiles that failed to load
    uint64_t cache_hits;           ///< Tiles loaded from cache
    uint64_t cache_misses;         ///< Tiles not in cache
    uint64_t bytes_downloaded;     ///< Total bytes downloaded (HTTP only)
    double average_load_time_ms;   ///< Average load time per tile
    uint64_t pending_loads;        ///< Currently pending async loads

    SRTMLoaderStats()
        : tiles_loaded(0),
          tiles_failed(0),
          cache_hits(0),
          cache_misses(0),
          bytes_downloaded(0),
          average_load_time_ms(0.0),
          pending_loads(0) {}
};

/// Configuration for SRTM loader
struct SRTMLoaderConfig {
    /// Data source type
    SRTMSource source = SRTMSource::LOCAL_DISK;

    /// Local directory containing .hgt files (for LOCAL_DISK source)
    std::string local_directory = "./srtm_data";

    /// URL template for HTTP downloads (for HTTP source)
    /// Placeholders: {lat} = latitude with N/S, {lon} = longitude with E/W
    /// Example: "https://server.com/srtm/{lat}{lon}.hgt"
    std::string url_template;

    /// Preferred SRTM resolution
    SRTMResolution preferred_resolution = SRTMResolution::SRTM3;

    /// Allow fallback to other resolution if preferred not available
    bool allow_resolution_fallback = true;

    /// Maximum concurrent downloads (HTTP only)
    size_t max_concurrent_downloads = 2;

    /// HTTP timeout in seconds
    uint32_t timeout_seconds = 60;

    /// Maximum retry attempts for failed downloads
    uint32_t max_retries = 3;

    /// Enable void filling during parsing
    bool fill_voids = true;

    /// User agent string for HTTP requests
    std::string user_agent = "EarthMap/1.0";
};

/// SRTM tile loader interface
/// Loads SRTM elevation data from disk or HTTP sources
class SRTMLoader {
public:
    virtual ~SRTMLoader() = default;

    /// Initialize loader with configuration
    /// @param config Loader configuration
    /// @return True on success
    virtual bool Initialize(const SRTMLoaderConfig& config) = 0;

    /// Load SRTM tile synchronously
    /// @param coordinates Tile coordinates to load
    /// @return Load result with tile data or error
    virtual SRTMLoadResult LoadTile(const SRTMCoordinates& coordinates) = 0;

    /// Load SRTM tile asynchronously
    /// @param coordinates Tile coordinates to load
    /// @param callback Optional callback when load completes
    /// @return Future that resolves to load result
    virtual std::future<SRTMLoadResult> LoadTileAsync(
        const SRTMCoordinates& coordinates,
        SRTMLoadCallback callback = nullptr) = 0;

    /// Load multiple tiles asynchronously
    /// @param coordinates Vector of tile coordinates to load
    /// @param callback Optional callback for each completed tile
    /// @return Vector of futures for each tile
    virtual std::vector<std::future<SRTMLoadResult>> LoadTilesAsync(
        const std::vector<SRTMCoordinates>& coordinates,
        SRTMLoadCallback callback = nullptr) = 0;

    /// Cancel pending tile load
    /// @param coordinates Tile coordinates to cancel
    /// @return True if load was cancelled
    virtual bool CancelLoad(const SRTMCoordinates& coordinates) = 0;

    /// Cancel all pending loads
    virtual void CancelAllLoads() = 0;

    /// Get loader statistics
    /// @return Current statistics
    virtual SRTMLoaderStats GetStatistics() const = 0;

    /// Get current configuration
    /// @return Loader configuration
    virtual SRTMLoaderConfig GetConfiguration() const = 0;

    /// Update configuration
    /// @param config New configuration
    /// @return True on success
    virtual bool SetConfiguration(const SRTMLoaderConfig& config) = 0;

    /// Check if tile is currently being loaded
    /// @param coordinates Tile coordinates to check
    /// @return True if load is pending
    virtual bool IsLoading(const SRTMCoordinates& coordinates) const = 0;

    /// Get number of pending loads
    /// @return Count of pending async loads
    virtual size_t GetPendingLoadCount() const = 0;

    /// Create SRTM loader instance
    /// @param config Initial configuration
    /// @return Unique pointer to loader instance
    [[nodiscard]] static std::unique_ptr<SRTMLoader> Create(
        const SRTMLoaderConfig& config);
};

} // namespace earth_map
