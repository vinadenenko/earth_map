// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#pragma once

#include "elevation_data.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace earth_map {

/// Configuration for elevation cache
struct ElevationCacheConfig {
    /// Maximum memory cache size in bytes (default: 200MB)
    size_t max_memory_cache_size = 200 * 1024 * 1024;

    /// Maximum disk cache size in bytes (default: 1GB)
    size_t max_disk_cache_size = 1024 * 1024 * 1024;

    /// Directory for disk cache
    std::string disk_cache_directory = "./elevation_cache";

    /// Time-to-live for cached tiles in seconds (default: 30 days)
    uint64_t tile_ttl_seconds = 30 * 24 * 3600;

    /// Enable compression for disk cache
    bool enable_compression = true;

    /// Enable disk cache (if false, memory-only cache)
    bool enable_disk_cache = true;
};

/// Statistics for elevation cache
struct ElevationCacheStats {
    uint64_t memory_cache_hits;      ///< Memory cache hits
    uint64_t disk_cache_hits;        ///< Disk cache hits
    uint64_t cache_misses;           ///< Total cache misses
    size_t memory_cache_size_bytes;  ///< Current memory cache size
    size_t disk_cache_size_bytes;    ///< Current disk cache size
    size_t tile_count_memory;        ///< Tiles in memory cache
    size_t tile_count_disk;          ///< Tiles in disk cache
    uint64_t evictions;              ///< Total evictions (LRU)

    ElevationCacheStats()
        : memory_cache_hits(0),
          disk_cache_hits(0),
          cache_misses(0),
          memory_cache_size_bytes(0),
          disk_cache_size_bytes(0),
          tile_count_memory(0),
          tile_count_disk(0),
          evictions(0) {}
};

/// Elevation cache interface
/// Provides two-level caching: fast memory cache + persistent disk cache
class ElevationCache {
public:
    virtual ~ElevationCache() = default;

    /// Initialize cache with configuration
    /// @param config Cache configuration
    /// @return True on success
    virtual bool Initialize(const ElevationCacheConfig& config) = 0;

    /// Store tile in cache
    /// @param tile_data Tile to store
    /// @return True on success
    virtual bool Put(const SRTMTileData& tile_data) = 0;

    /// Retrieve tile from cache
    /// @param coordinates Tile coordinates to retrieve
    /// @return Shared pointer to tile data, or nullopt if not in cache
    virtual std::optional<std::shared_ptr<SRTMTileData>> Get(
        const SRTMCoordinates& coordinates) = 0;

    /// Check if tile exists in cache (without loading)
    /// @param coordinates Tile coordinates to check
    /// @return True if tile is cached
    virtual bool Contains(const SRTMCoordinates& coordinates) const = 0;

    /// Remove tile from cache
    /// @param coordinates Tile coordinates to remove
    /// @return True if tile was removed
    virtual bool Remove(const SRTMCoordinates& coordinates) = 0;

    /// Clear all cached tiles
    virtual void Clear() = 0;

    /// Clear memory cache only (disk cache remains)
    virtual void ClearMemoryCache() = 0;

    /// Clear disk cache only (memory cache remains)
    virtual void ClearDiskCache() = 0;

    /// Get cache statistics
    /// @return Current statistics
    virtual ElevationCacheStats GetStatistics() const = 0;

    /// Get current configuration
    /// @return Cache configuration
    virtual ElevationCacheConfig GetConfiguration() const = 0;

    /// Update configuration (may trigger cache resizing)
    /// @param config New configuration
    /// @return True on success
    virtual bool SetConfiguration(const ElevationCacheConfig& config) = 0;

    /// Flush memory cache to disk
    /// @return Number of tiles written to disk
    virtual size_t Flush() = 0;

    /// Prune expired tiles from disk cache
    /// @return Number of tiles removed
    virtual size_t PruneExpired() = 0;

    /// Create elevation cache instance
    /// @param config Initial configuration
    /// @return Unique pointer to cache instance
    [[nodiscard]] static std::unique_ptr<ElevationCache> Create(
        const ElevationCacheConfig& config);
};

} // namespace earth_map
