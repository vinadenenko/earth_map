// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/data/elevation_cache.h>
#include <earth_map/data/hgt_parser.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace earth_map {

namespace {

/// LRU cache entry
struct CacheEntry {
    SRTMCoordinates coordinates;
    std::shared_ptr<SRTMTileData> tile_data;
    size_t size_bytes;
    std::chrono::system_clock::time_point timestamp;

    CacheEntry(const SRTMCoordinates& coords,
               std::shared_ptr<SRTMTileData> data,
               size_t size)
        : coordinates(coords),
          tile_data(std::move(data)),
          size_bytes(size),
          timestamp(std::chrono::system_clock::now()) {}
};

/// Format cache filename from coordinates
std::string FormatCacheFilename(const SRTMCoordinates& coords) {
    std::ostringstream oss;
    oss << (coords.latitude >= 0 ? 'N' : 'S')
        << std::abs(coords.latitude)
        << (coords.longitude >= 0 ? 'E' : 'W')
        << std::abs(coords.longitude)
        << ".hgt";
    return oss.str();
}

} // anonymous namespace

/// Basic elevation cache implementation with LRU eviction
class BasicElevationCache : public ElevationCache {
public:
    explicit BasicElevationCache(const ElevationCacheConfig& config)
        : config_(config) {}

    bool Initialize(const ElevationCacheConfig& config) override {
        config_ = config;

        // Create disk cache directory if enabled
        if (config_.enable_disk_cache) {
            try {
                std::filesystem::create_directories(config_.disk_cache_directory);
            } catch (...) {
                return false;
            }
        }

        return true;
    }

    bool Put(const SRTMTileData& tile_data) override {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        const auto& coords = tile_data.GetMetadata().coordinates;
        const size_t tile_size = tile_data.GetMetadata().file_size;

        // Remove existing entry if present
        RemoveFromMemoryCache(coords);

        // Check if we need to evict entries to make room
        while (stats_.memory_cache_size_bytes + tile_size > config_.max_memory_cache_size &&
               !lru_list_.empty()) {
            EvictLRU();
        }

        // Create shared pointer and add to memory cache
        auto tile_ptr = std::make_shared<SRTMTileData>(tile_data.GetMetadata());
        tile_ptr->GetRawData() = tile_data.GetRawData();
        tile_ptr->SetValid(tile_data.IsValid());
        tile_ptr->SetHasVoids(tile_data.GetMetadata().has_voids);

        auto entry = std::make_shared<CacheEntry>(coords, tile_ptr, tile_size);

        lru_list_.push_front(entry);
        memory_cache_[coords] = lru_list_.begin();

        stats_.memory_cache_size_bytes += tile_size;
        stats_.tile_count_memory = memory_cache_.size();

        // Write to disk cache if enabled
        if (config_.enable_disk_cache) {
            WriteToDiskCache(tile_data);
        }

        return true;
    }

    std::optional<std::shared_ptr<SRTMTileData>> Get(
        const SRTMCoordinates& coordinates) override {

        std::lock_guard<std::mutex> lock(cache_mutex_);

        // Check memory cache first
        auto it = memory_cache_.find(coordinates);
        if (it != memory_cache_.end()) {
            // Move to front of LRU list
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
            ++stats_.memory_cache_hits;
            return (*it->second)->tile_data;
        }

        // Check disk cache if enabled
        if (config_.enable_disk_cache) {
            auto tile_data = ReadFromDiskCache(coordinates);
            if (tile_data) {
                ++stats_.disk_cache_hits;

                // Add to memory cache
                const size_t tile_size = tile_data->GetMetadata().file_size;

                // Evict if needed
                while (stats_.memory_cache_size_bytes + tile_size >
                           config_.max_memory_cache_size &&
                       !lru_list_.empty()) {
                    EvictLRU();
                }

                // Add to memory cache
                auto entry = std::make_shared<CacheEntry>(
                    coordinates, tile_data, tile_size);

                lru_list_.push_front(entry);
                memory_cache_[coordinates] = lru_list_.begin();
                stats_.memory_cache_size_bytes += tile_size;
                stats_.tile_count_memory = memory_cache_.size();

                return tile_data;
            }
        }

        ++stats_.cache_misses;
        return std::nullopt;
    }

    bool Contains(const SRTMCoordinates& coordinates) const override {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        // Check memory cache
        if (memory_cache_.find(coordinates) != memory_cache_.end()) {
            return true;
        }

        // Check disk cache
        if (config_.enable_disk_cache) {
            const std::string filename = FormatCacheFilename(coordinates);
            const std::string filepath = config_.disk_cache_directory + "/" + filename;
            return std::filesystem::exists(filepath);
        }

        return false;
    }

    bool Remove(const SRTMCoordinates& coordinates) override {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        bool removed = RemoveFromMemoryCache(coordinates);

        // Remove from disk cache
        if (config_.enable_disk_cache) {
            const std::string filename = FormatCacheFilename(coordinates);
            const std::string filepath = config_.disk_cache_directory + "/" + filename;

            try {
                if (std::filesystem::remove(filepath)) {
                    removed = true;
                }
            } catch (...) {
                // Ignore errors
            }
        }

        return removed;
    }

    void Clear() override {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        lru_list_.clear();
        memory_cache_.clear();
        stats_.memory_cache_size_bytes = 0;
        stats_.tile_count_memory = 0;

        if (config_.enable_disk_cache) {
            try {
                std::filesystem::remove_all(config_.disk_cache_directory);
                std::filesystem::create_directories(config_.disk_cache_directory);
                stats_.disk_cache_size_bytes = 0;
                stats_.tile_count_disk = 0;
            } catch (...) {
                // Ignore errors
            }
        }
    }

    void ClearMemoryCache() override {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        lru_list_.clear();
        memory_cache_.clear();
        stats_.memory_cache_size_bytes = 0;
        stats_.tile_count_memory = 0;
    }

    void ClearDiskCache() override {
        if (!config_.enable_disk_cache) {
            return;
        }

        std::lock_guard<std::mutex> lock(cache_mutex_);

        try {
            std::filesystem::remove_all(config_.disk_cache_directory);
            std::filesystem::create_directories(config_.disk_cache_directory);
            stats_.disk_cache_size_bytes = 0;
            stats_.tile_count_disk = 0;
        } catch (...) {
            // Ignore errors
        }
    }

    ElevationCacheStats GetStatistics() const override {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return stats_;
    }

    ElevationCacheConfig GetConfiguration() const override {
        return config_;
    }

    bool SetConfiguration(const ElevationCacheConfig& config) override {
        std::lock_guard<std::mutex> lock(cache_mutex_);

        const bool disk_cache_changed =
            (config.enable_disk_cache != config_.enable_disk_cache) ||
            (config.disk_cache_directory != config_.disk_cache_directory);

        config_ = config;

        // Clear memory cache if size limit decreased
        while (stats_.memory_cache_size_bytes > config_.max_memory_cache_size &&
               !lru_list_.empty()) {
            EvictLRU();
        }

        // Create disk cache directory if enabled
        if (config_.enable_disk_cache && disk_cache_changed) {
            try {
                std::filesystem::create_directories(config_.disk_cache_directory);
            } catch (...) {
                return false;
            }
        }

        return true;
    }

    size_t Flush() override {
        if (!config_.enable_disk_cache) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(cache_mutex_);

        size_t flushed = 0;

        for (const auto& pair : memory_cache_) {
            const auto& entry = *pair.second;
            if (WriteToDiskCache(*entry->tile_data)) {
                ++flushed;
            }
        }

        return flushed;
    }

    size_t PruneExpired() override {
        if (!config_.enable_disk_cache) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(cache_mutex_);

        size_t pruned = 0;
        const auto now = std::chrono::system_clock::now();
        const auto ttl = std::chrono::seconds(config_.tile_ttl_seconds);

        try {
            for (const auto& entry : std::filesystem::directory_iterator(
                     config_.disk_cache_directory)) {
                if (entry.is_regular_file()) {
                    const auto file_time = std::filesystem::last_write_time(entry);
                    const auto system_time = std::chrono::time_point_cast<
                        std::chrono::system_clock::duration>(
                        file_time - std::filesystem::file_time_type::clock::now() + now);

                    if (now - system_time > ttl) {
                        std::filesystem::remove(entry.path());
                        ++pruned;
                    }
                }
            }
        } catch (...) {
            // Ignore errors
        }

        return pruned;
    }

private:
    bool RemoveFromMemoryCache(const SRTMCoordinates& coordinates) {
        auto it = memory_cache_.find(coordinates);
        if (it != memory_cache_.end()) {
            const size_t tile_size = (*it->second)->size_bytes;
            lru_list_.erase(it->second);
            memory_cache_.erase(it);

            stats_.memory_cache_size_bytes -= tile_size;
            stats_.tile_count_memory = memory_cache_.size();
            return true;
        }
        return false;
    }

    void EvictLRU() {
        if (lru_list_.empty()) {
            return;
        }

        auto& entry = lru_list_.back();
        const size_t tile_size = entry->size_bytes;

        memory_cache_.erase(entry->coordinates);
        lru_list_.pop_back();

        stats_.memory_cache_size_bytes -= tile_size;
        stats_.tile_count_memory = memory_cache_.size();
        ++stats_.evictions;
    }

    bool WriteToDiskCache(const SRTMTileData& tile_data) {
        const auto& coords = tile_data.GetMetadata().coordinates;
        const std::string filename = FormatCacheFilename(coords);
        const std::string filepath = config_.disk_cache_directory + "/" + filename;

        try {
            std::ofstream file(filepath, std::ios::binary);
            if (!file.is_open()) {
                return false;
            }

            const auto& raw_data = tile_data.GetRawData();

            // Convert to big-endian format (HGT standard) before writing
            std::vector<uint8_t> big_endian_data(raw_data.size() * 2);
            for (size_t i = 0; i < raw_data.size(); ++i) {
                const int16_t value = raw_data[i];
                // Write high byte first (big-endian)
                big_endian_data[i * 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
                big_endian_data[i * 2 + 1] = static_cast<uint8_t>(value & 0xFF);
            }

            file.write(reinterpret_cast<const char*>(big_endian_data.data()),
                       big_endian_data.size());

            return file.good();
        } catch (...) {
            return false;
        }
    }

    std::shared_ptr<SRTMTileData> ReadFromDiskCache(
        const SRTMCoordinates& coordinates) {

        const std::string filename = FormatCacheFilename(coordinates);
        const std::string filepath = config_.disk_cache_directory + "/" + filename;

        if (!std::filesystem::exists(filepath)) {
            return nullptr;
        }

        // Use HGT parser to read the file
        // Note: Cache files use same format as HGT files
        return HGTParser::ParseFile(filepath);
    }

    ElevationCacheConfig config_;
    ElevationCacheStats stats_;

    mutable std::mutex cache_mutex_;

    // LRU cache data structures
    using LRUList = std::list<std::shared_ptr<CacheEntry>>;
    LRUList lru_list_;
    std::unordered_map<SRTMCoordinates, LRUList::iterator> memory_cache_;
};

std::unique_ptr<ElevationCache> ElevationCache::Create(
    const ElevationCacheConfig& config) {

    auto cache = std::make_unique<BasicElevationCache>(config);
    if (!cache->Initialize(config)) {
        return nullptr;
    }
    return cache;
}

} // namespace earth_map
