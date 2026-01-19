#pragma once

/**
 * @file tile_cache.h
 * @brief Tile caching system for efficient tile storage and retrieval
 * 
 * Provides disk-based and memory-based tile caching with LRU eviction,
 * metadata management, and compression support for globe rendering.
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/math/bounding_box.h>
#include <glm/vec2.hpp>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <chrono>
#include <functional>
#include <optional>

namespace earth_map {

/**
 * @brief Tile metadata information
 */
struct TileMetadata {
    /** Tile coordinates */
    TileCoordinates coordinates;
    
    /** File size in bytes */
    std::size_t file_size = 0;
    
    /** Last modified timestamp */
    std::chrono::system_clock::time_point last_modified;
    
    /** Expiration timestamp */
    std::chrono::system_clock::time_point expires_at;
    
    /** ETag from HTTP header for validation */
    std::string etag;
    
    /** Content type */
    std::string content_type;
    
    /** Compression type */
    enum class Compression {
        NONE,       ///< No compression
        GZIP,       ///< GZIP compression
        DEFLATE,    ///< DEFLATE compression
        BROTLI      ///< Brotli compression
    } compression = Compression::NONE;
    
    /** Checksum for data integrity */
    std::uint32_t checksum = 0;
    
    /** Access count for LRU tracking */
    std::uint64_t access_count = 0;
    
    /** Last access timestamp */
    std::chrono::system_clock::time_point last_access;
    
    /**
     * @brief Default constructor
     */
    TileMetadata() = default;
    
    /**
     * @brief Constructor with coordinates
     */
    explicit TileMetadata(const TileCoordinates& coords) 
        : coordinates(coords) {}
};

/**
 * @brief Tile data container
 */
struct TileData {
    /** Tile metadata */
    TileMetadata metadata;
    
    /** Raw tile data bytes */
    std::vector<std::uint8_t> data;
    
    /** Whether data is compressed */
    bool is_compressed = false;
    
    /** Image width (if applicable) */
    std::uint32_t width = 0;
    
    /** Image height (if applicable) */
    std::uint32_t height = 0;
    
    /** Number of channels (if applicable) */
    std::uint8_t channels = 0;
    
    /** Loading state */
    bool loaded = false;
    
    /**
     * @brief Default constructor
     */
    TileData() = default;
    
    /**
     * @brief Constructor with metadata
     */
    explicit TileData(const TileMetadata& meta) 
        : metadata(meta) {}
    
    /**
     * @brief Check if tile data is valid
     */
    bool IsValid() const {
        return !data.empty() && metadata.file_size > 0;
    }
    
    /**
     * @brief Get data size
     */
    std::size_t GetDataSize() const {
        return data.size();
    }
};

/**
 * @brief Tile cache configuration
 */
struct TileCacheConfig {
    /** Maximum memory cache size in bytes */
    std::size_t max_memory_cache_size = 100 * 1024 * 1024;  // 100MB
    
    /** Maximum disk cache size in bytes */
    std::size_t max_disk_cache_size = 1024 * 1024 * 1024;   // 1GB
    
    /** Disk cache directory path */
    std::string disk_cache_directory = "./tile_cache";
    
    /** Cache eviction strategy */
    enum class EvictionStrategy {
        LRU,        ///< Least Recently Used
        LFU,        ///< Least Frequently Used
        SIZE_BASED, ///< Largest files first
        TIME_BASED  ///< Oldest files first
    } eviction_strategy = EvictionStrategy::LRU;
    
    /** Enable compression for disk cache */
    bool enable_compression = true;
    
    /** Default compression type */
    TileMetadata::Compression default_compression = TileMetadata::Compression::GZIP;
    
    /** TTL for cached tiles in seconds */
    std::uint64_t tile_ttl = 7 * 24 * 3600;  // 7 days
    
    /** Enable integrity checking */
    bool enable_integrity_check = true;
    
    /** Maximum number of tiles to cache */
    std::size_t max_tile_count = 10000;
    
    /** Cache cleanup interval in seconds */
    std::uint64_t cleanup_interval = 3600;  // 1 hour
};

/**
 * @brief Tile cache statistics
 */
struct TileCacheStats {
    /** Memory cache statistics */
    std::size_t memory_cache_size = 0;
    std::size_t memory_cache_count = 0;
    std::size_t memory_cache_hits = 0;
    std::size_t memory_cache_misses = 0;
    
    /** Disk cache statistics */
    std::size_t disk_cache_size = 0;
    std::size_t disk_cache_count = 0;
    std::size_t disk_cache_hits = 0;
    std::size_t disk_cache_misses = 0;
    
    /** Overall statistics */
    std::size_t total_requests = 0;
    std::size_t total_evictions = 0;
    std::size_t total_corruptions = 0;
    
    /** Cache hit ratio */
    float GetHitRatio() const {
        return total_requests > 0 ? 
            static_cast<float>(memory_cache_hits + disk_cache_hits) / total_requests : 0.0f;
    }
    
    /**
     * @brief Reset statistics
     */
    void Reset() {
        memory_cache_size = 0;
        memory_cache_count = 0;
        memory_cache_hits = 0;
        memory_cache_misses = 0;
        disk_cache_size = 0;
        disk_cache_count = 0;
        disk_cache_hits = 0;
        disk_cache_misses = 0;
        total_requests = 0;
        total_evictions = 0;
        total_corruptions = 0;
    }
};

/**
 * @brief Tile cache interface
 * 
 * Provides efficient caching of tile data with memory and disk storage,
 * LRU eviction, compression, and integrity checking.
 */
class TileCache {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~TileCache() = default;
    
    /**
     * @brief Initialize the tile cache
     * 
     * @param config Configuration parameters
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize(const TileCacheConfig& config) = 0;
    
    /**
     * @brief Put tile data into cache
     *
     * @param tile_data Tile data to store
     * @return true if storage succeeded, false otherwise
     */
    virtual bool Put(const TileData& tile_data) = 0;

    /**
     * @brief Get tile data from cache
     *
     * @param coordinates Tile coordinates to retrieve
     * @return std::optional<TileData> Tile data if found, std::nullopt otherwise
     */
    virtual std::optional<TileData> Get(const TileCoordinates& coordinates) = 0;
    
    /**
     * @brief Check if tile exists in cache
     * 
     * @param coordinates Tile coordinates to check
     * @return true if tile exists, false otherwise
     */
    virtual bool Contains(const TileCoordinates& coordinates) const = 0;
    
    /**
     * @brief Remove tile from cache
     * 
     * @param coordinates Tile coordinates to remove
     * @return true if removal succeeded, false otherwise
     */
    virtual bool Remove(const TileCoordinates& coordinates) = 0;
    
    /**
     * @brief Clear all cached tiles
     */
    virtual void Clear() = 0;
    
    /**
     * @brief Get cache statistics
     * 
     * @return TileCacheStats Current cache statistics
     */
    virtual TileCacheStats GetStatistics() const = 0;
    
    /**
     * @brief Update tile metadata
     * 
     * @param coordinates Tile coordinates
     * @param metadata New metadata
     * @return true if update succeeded, false otherwise
     */
    virtual bool UpdateMetadata(const TileCoordinates& coordinates, 
                               const TileMetadata& metadata) = 0;
    
    /**
     * @brief Get tile metadata
     * 
     * @param coordinates Tile coordinates
     * @return std::shared_ptr<TileMetadata> Tile metadata or nullptr if not found
     */
    virtual std::shared_ptr<TileMetadata> GetMetadata(
        const TileCoordinates& coordinates) const = 0;
    
    /**
     * @brief Perform cache cleanup and maintenance
     * 
     * @return std::size_t Number of tiles cleaned up
     */
    virtual std::size_t Cleanup() = 0;
    
    /**
     * @brief Get cache configuration
     * 
     * @return TileCacheConfig Current configuration
     */
    virtual TileCacheConfig GetConfiguration() const = 0;
    
    /**
     * @brief Set cache configuration
     * 
     * @param config New configuration
     * @return true if configuration was applied, false otherwise
     */
    virtual bool SetConfiguration(const TileCacheConfig& config) = 0;
    
    /**
     * @brief Preload tiles into memory cache
     * 
     * @param coordinates List of tile coordinates to preload
     * @return std::size_t Number of tiles successfully preloaded
     */
    virtual std::size_t Preload(const std::vector<TileCoordinates>& coordinates) = 0;
    
    /**
     * @brief Get tiles cached in specified geographic bounds
     * 
     * @param bounds Geographic bounds to query
     * @return std::vector<TileCoordinates> List of cached tile coordinates
     */
    virtual std::vector<TileCoordinates> GetTilesInBounds(
        const BoundingBox2D& bounds) const = 0;
    
    /**
     * @brief Get tiles cached at specific zoom level
     * 
     * @param zoom_level Zoom level to query
     * @return std::vector<TileCoordinates> List of cached tile coordinates
     */
    virtual std::vector<TileCoordinates> GetTilesAtZoom(
        std::uint8_t zoom_level) const = 0;

protected:
    /**
     * @brief Protected constructor
     */
    TileCache() = default;
};

/**
 * @brief Factory function to create tile cache
 * 
 * @param config Configuration parameters
 * @return std::unique_ptr<TileCache> New tile cache instance
 */
std::unique_ptr<TileCache> CreateTileCache(const TileCacheConfig& config = {});

} // namespace earth_map