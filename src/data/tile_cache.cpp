/**
 * @file tile_cache.cpp
 * @brief Tile caching system implementation
 */

#include <earth_map/data/tile_cache.h>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <random>
#include <spdlog/spdlog.h>
#include <string_view>

namespace earth_map {
namespace {

std::string EncodeCachePathComponent(std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";

    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        encoded.push_back(kHex[byte >> 4U]);
        encoded.push_back(kHex[byte & 0x0fU]);
    }
    return encoded;
}

}  // namespace



/**
 * @brief Basic tile cache implementation
 */
class BasicTileCache : public TileCache {
public:
    explicit BasicTileCache(const TileCacheConfig& config) : config_(config) {}
    ~BasicTileCache() override = default;
    
    bool Initialize(const TileCacheConfig& config) override;
    bool Put(const TileData& tile_data) override;
    std::optional<TileData> Get(const imagery::ImageTileKey& key) override;
    bool Contains(const imagery::ImageTileKey& key) const override;
    bool Remove(const imagery::ImageTileKey& key) override;
    void Clear() override;
    
    TileCacheStats GetStatistics() const override;
    bool UpdateMetadata(const imagery::ImageTileKey& key,
                        const TileMetadata& metadata) override;
    std::shared_ptr<TileMetadata> GetMetadata(
        const imagery::ImageTileKey& key) const override;
    std::size_t Cleanup() override;
    
    TileCacheConfig GetConfiguration() const override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return config_;
    }
    bool SetConfiguration(const TileCacheConfig& config) override;
    
    std::size_t Preload(const std::vector<imagery::ImageTileKey>& keys) override;

private:
    // Coarse-grained lock protecting all mutable state. The LRU implementation
    // (vector + index map) is O(n) per access and should be replaced with
    // std::list + iterator map for O(1) when the cache is redesigned for
    // texture streaming. For now this mutex prevents the data races.
    mutable std::recursive_mutex mutex_;

    TileCacheConfig config_;
    TileCacheStats stats_;
    
    // Memory cache
    std::unordered_map<imagery::ImageTileKey, std::shared_ptr<TileData>,
                       imagery::ImageTileKeyHash> memory_cache_;
    std::unordered_map<imagery::ImageTileKey, std::shared_ptr<TileMetadata>,
                       imagery::ImageTileKeyHash> metadata_cache_;
    
    // LRU tracking
    mutable std::vector<imagery::ImageTileKey> lru_list_;
    mutable std::unordered_map<imagery::ImageTileKey, std::size_t,
                               imagery::ImageTileKeyHash> lru_index_;
    
    std::string GetTileFilePath(const imagery::ImageTileKey& key) const;
    std::string GetMetadataFilePath(const imagery::ImageTileKey& key) const;
    std::vector<std::uint8_t> CompressData(const std::vector<std::uint8_t>& data,
                                           TileMetadata::Compression type) const;
    std::vector<std::uint8_t> DecompressData(const std::vector<std::uint8_t>& data,
                                             TileMetadata::Compression type) const;
    std::uint32_t CalculateChecksum(const std::vector<std::uint8_t>& data) const;
    bool SaveTileToDisk(const TileData& tile_data) const;
    std::unique_ptr<TileData> LoadTileFromDisk(const imagery::ImageTileKey& key) const;
    bool SaveMetadataToDisk(const TileMetadata& metadata) const;
    std::unique_ptr<TileMetadata> LoadMetadataFromDisk(const imagery::ImageTileKey& key) const;
    void UpdateLRU(const imagery::ImageTileKey& key) const;
    void EvictFromMemory(std::size_t required_space);
    void EvictFromDisk(std::size_t required_space);
    bool IsTileExpired(const TileMetadata& metadata) const;
    std::size_t CalculateCurrentMemoryUsage() const;
    std::size_t CalculateCurrentDiskUsage() const;
};

// Factory function
std::unique_ptr<TileCache> CreateTileCache(const TileCacheConfig& config) {
    return std::make_unique<BasicTileCache>(config);
}

bool BasicTileCache::Initialize(const TileCacheConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    config_ = config;
    stats_.Reset();
    
    // Create disk cache directory if it doesn't exist
    try {
        std::filesystem::create_directories(config_.disk_cache_directory);
        
        // Version one stores each source/matrix namespace separately.  Do not
        // read the historical bare-XYZ layout: it can alias different imagery
        // providers at the same level/x/y address.
        std::filesystem::create_directories(
            config_.disk_cache_directory + "/imagery-v1");
        
        spdlog::info("Tile cache initialized. Memory: {}MB, Disk: {}MB, Directory: {}",
                     config_.max_memory_cache_size / (1024 * 1024),
                     config_.max_disk_cache_size / (1024 * 1024),
                     config_.disk_cache_directory);
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize tile cache: {}", e.what());
        return false;
    }
}

bool BasicTileCache::Put(const TileData& tile_data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!tile_data.IsValid()) {
        spdlog::warn("Attempted to store invalid tile data");
        return false;
    }

    const auto& key = tile_data.metadata.imagery_key;
    if (!key.IsValid()) {
        spdlog::warn("Attempted to store tile data without an imagery key");
        return false;
    }
    stats_.total_requests++;

    // Update metadata
    metadata_cache_[key] = std::make_shared<TileMetadata>(tile_data.metadata);
    SaveMetadataToDisk(tile_data.metadata);

    // Store in memory cache. Replacing a refreshed tile must not inflate the
    // resident byte/count accounting.
    if (const auto existing = memory_cache_.find(key); existing != memory_cache_.end()) {
        stats_.memory_cache_size -= existing->second->GetDataSize();
    } else {
        stats_.memory_cache_count++;
    }
    auto data_copy = std::make_shared<TileData>(tile_data);
    memory_cache_[key] = data_copy;

    // Update LRU
    UpdateLRU(key);

    // Update statistics
    stats_.memory_cache_size += tile_data.GetDataSize();

    // Save to disk cache as well
    if (!SaveTileToDisk(tile_data)) {
        spdlog::warn("Failed to save tile to disk cache: {}/{}/{}/{}",
                     key.imagery_source_id, key.matrix_set_id,
                     key.address.level, key.address.column);
    }

    // Check if we need to evict from memory
    std::size_t current_usage = CalculateCurrentMemoryUsage();
    if (current_usage > config_.max_memory_cache_size) {
        EvictFromMemory(current_usage - config_.max_memory_cache_size);
    }

    return true;
}

std::optional<TileData> BasicTileCache::Get(const imagery::ImageTileKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!key.IsValid()) {
        return std::nullopt;
    }
    stats_.total_requests++;

    // First try memory cache
    auto it = memory_cache_.find(key);
    if (it != memory_cache_.end()) {
        stats_.memory_cache_hits++;
        UpdateLRU(key);

        // Update access metadata
        if (auto metadata = GetMetadata(key)) {
            metadata->access_count++;
            metadata->last_access = std::chrono::system_clock::now();
        }

        return *(it->second);  // Return copy of TileData
    }

    stats_.memory_cache_misses++;

    // Try loading from disk
    auto disk_tile = LoadTileFromDisk(key);
    if (disk_tile && disk_tile->IsValid()) {
        stats_.disk_cache_hits++;

        // Add to memory cache
        auto shared_tile = std::shared_ptr<TileData>(disk_tile.release());
        memory_cache_[key] = shared_tile;
        UpdateLRU(key);

        // Update statistics
        stats_.memory_cache_size += shared_tile->GetDataSize();
        stats_.memory_cache_count++;

        return *shared_tile;  // Return copy of TileData
    }

    stats_.disk_cache_misses++;
    return std::nullopt;
}

bool BasicTileCache::Contains(const imagery::ImageTileKey& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!key.IsValid()) {
        return false;
    }
    // Check memory cache first
    if (memory_cache_.find(key) != memory_cache_.end()) {
        return true;
    }
    
    // Check disk cache
    std::string file_path = GetTileFilePath(key);
    return std::filesystem::exists(file_path);
}

bool BasicTileCache::Remove(const imagery::ImageTileKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!key.IsValid()) {
        return false;
    }
    bool removed_memory = false;
    bool removed_disk = false;
    
    // Remove from memory cache
    auto mem_it = memory_cache_.find(key);
    if (mem_it != memory_cache_.end()) {
        stats_.memory_cache_size -= mem_it->second->GetDataSize();
        stats_.memory_cache_count--;
        memory_cache_.erase(mem_it);
        removed_memory = true;
    }
    
    // Remove from metadata cache
    metadata_cache_.erase(key);
    
    // Remove from LRU
    auto lru_it = lru_index_.find(key);
    if (lru_it != lru_index_.end()) {
        std::size_t index = lru_it->second;
        lru_list_.erase(lru_list_.begin() + index);
        lru_index_.erase(lru_it);
        
        // Update indices
        for (std::size_t i = index; i < lru_list_.size(); ++i) {
            lru_index_[lru_list_[i]] = i;
        }
    }
    
    // Remove from disk
    std::string tile_path = GetTileFilePath(key);
    std::string meta_path = GetMetadataFilePath(key);
    
    try {
        removed_disk = std::filesystem::remove(tile_path) || 
                       std::filesystem::remove(meta_path);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to remove tile files: {}", e.what());
    }
    
    return removed_memory || removed_disk;
}

void BasicTileCache::Clear() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    memory_cache_.clear();
    metadata_cache_.clear();
    lru_list_.clear();
    lru_index_.clear();
    
    stats_.Reset();
    
    // Clear disk cache
    try {
        if (std::filesystem::exists(config_.disk_cache_directory)) {
            std::filesystem::remove_all(config_.disk_cache_directory);
            std::filesystem::create_directories(config_.disk_cache_directory);
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to clear disk cache: {}", e.what());
    }
    
    spdlog::info("Tile cache cleared");
}

TileCacheStats BasicTileCache::GetStatistics() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TileCacheStats stats = stats_;  // Copy current stats
    
    // Calculate disk usage (mutable operation but returns copy)
    stats.disk_cache_size = const_cast<BasicTileCache*>(this)->CalculateCurrentDiskUsage();
    stats.disk_cache_count = 0;
    
    // Count disk tiles
    try {
        for (const auto& entry : 
             std::filesystem::recursive_directory_iterator(config_.disk_cache_directory)) {
            if (entry.path().extension() == ".tile") {
                stats.disk_cache_count++;
            }
        }
    } catch (const std::exception&) {
        // Ignore directory iteration errors
    }
    
    return stats;
}

bool BasicTileCache::UpdateMetadata(const imagery::ImageTileKey& key,
                                    const TileMetadata& metadata) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!key.IsValid() || metadata.imagery_key != key) {
        return false;
    }
    metadata_cache_[key] = std::make_shared<TileMetadata>(metadata);
    return SaveMetadataToDisk(metadata);
}

std::shared_ptr<TileMetadata> BasicTileCache::GetMetadata(
    const imagery::ImageTileKey& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!key.IsValid()) {
        return nullptr;
    }
    // Check memory cache first
    auto it = metadata_cache_.find(key);
    if (it != metadata_cache_.end()) {
        return it->second;
    }
    
    // Load from disk
    auto disk_metadata = const_cast<BasicTileCache*>(this)->LoadMetadataFromDisk(key);
    if (disk_metadata) {
        auto shared_metadata = std::shared_ptr<TileMetadata>(disk_metadata.release());
        // Use const_cast to modify cache in const method - this is logical since
        // we're just adding a cached result, not changing the logical state
        const_cast<std::unordered_map<imagery::ImageTileKey, std::shared_ptr<TileMetadata>,
                                      imagery::ImageTileKeyHash>&>(metadata_cache_)[key] = shared_metadata;
        return shared_metadata;
    }
    
    return nullptr;
}

std::size_t BasicTileCache::Cleanup() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::size_t cleaned_count = 0;
    
    // Clean expired tiles
    for (auto it = memory_cache_.begin(); it != memory_cache_.end();) {
        if (IsTileExpired(it->second->metadata)) {
            Remove(it->first);
            ++cleaned_count;
            it = memory_cache_.begin(); // Restart iterator
        } else {
            ++it;
        }
    }
    
    if (cleaned_count > 0) {
        spdlog::info("Cache cleanup completed. Cleaned {} tiles", cleaned_count);
    }
    
    return cleaned_count;
}

bool BasicTileCache::SetConfiguration(const TileCacheConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    config_ = config;
    
    // Perform immediate cleanup if limits decreased
    if (CalculateCurrentMemoryUsage() > config_.max_memory_cache_size) {
        EvictFromMemory(CalculateCurrentMemoryUsage() - config_.max_memory_cache_size);
    }
    
    if (CalculateCurrentDiskUsage() > config_.max_disk_cache_size) {
        EvictFromDisk(CalculateCurrentDiskUsage() - config_.max_disk_cache_size);
    }
    
    return true;
}

std::size_t BasicTileCache::Preload(const std::vector<imagery::ImageTileKey>& keys) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::size_t loaded_count = 0;
    
    for (const auto& key : keys) {
        if (!key.IsValid() || memory_cache_.find(key) != memory_cache_.end()) {
            continue;
        }

        auto tile_data = LoadTileFromDisk(key);
        if (tile_data && tile_data->IsValid()) {
            auto shared_tile = std::shared_ptr<TileData>(tile_data.release());
            memory_cache_[key] = shared_tile;
            UpdateLRU(key);
            stats_.memory_cache_size += shared_tile->GetDataSize();
            stats_.memory_cache_count++;
            loaded_count++;
        }
    }
    
    if (loaded_count > 0) {
        spdlog::info("Preloaded {} tiles into memory cache", loaded_count);
    }
    
    return loaded_count;
}

std::string BasicTileCache::GetTileFilePath(const imagery::ImageTileKey& key) const {
    std::ostringstream oss;
    oss << config_.disk_cache_directory << "/imagery-v1/"
        << EncodeCachePathComponent(key.imagery_source_id) << "/"
        << EncodeCachePathComponent(key.matrix_set_id) << "/"
        << key.address.level << "/"
        << key.address.column << "_" << key.address.row << ".tile";
    return oss.str();
}

std::string BasicTileCache::GetMetadataFilePath(const imagery::ImageTileKey& key) const {
    std::ostringstream oss;
    oss << config_.disk_cache_directory << "/imagery-v1/"
        << EncodeCachePathComponent(key.imagery_source_id) << "/"
        << EncodeCachePathComponent(key.matrix_set_id) << "/"
        << key.address.level << "/"
        << key.address.column << "_" << key.address.row << ".meta";
    return oss.str();
}

std::vector<std::uint8_t> BasicTileCache::CompressData(
    const std::vector<std::uint8_t>& data,
    TileMetadata::Compression type) const {
    
    // TODO: Implement actual compression based on type
    // For now, return data as-is
    (void)type;  // Suppress unused parameter warning
    return data;
}

std::vector<std::uint8_t> BasicTileCache::DecompressData(
    const std::vector<std::uint8_t>& data,
    TileMetadata::Compression type) const {
    
    // TODO: Implement actual decompression based on type
    // For now, return data as-is
    (void)type;  // Suppress unused parameter warning
    return data;
}

std::uint32_t BasicTileCache::CalculateChecksum(
    const std::vector<std::uint8_t>& data) const {
    
    // Simple checksum calculation
    std::uint32_t checksum = 0;
    for (std::uint8_t byte : data) {
        checksum = checksum * 31 + byte;
    }
    return checksum;
}

bool BasicTileCache::SaveTileToDisk(const TileData& tile_data) const {
    const auto& key = tile_data.metadata.imagery_key;
    std::string file_path = GetTileFilePath(key);
    
    try {
        std::filesystem::create_directories(std::filesystem::path(file_path).parent_path());
        std::ofstream file(file_path, std::ios::binary);
        if (!file) {
            return false;
        }
        
        // Write data size
        std::uint64_t data_size = tile_data.data.size();
        file.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
        
        // Write actual data
        file.write(reinterpret_cast<const char*>(tile_data.data.data()), 
                   data_size);
        
        return file.good();
    } catch (const std::exception& e) {
        spdlog::error("Failed to save tile to disk: {}", e.what());
        return false;
    }
}

std::unique_ptr<TileData> BasicTileCache::LoadTileFromDisk(
    const imagery::ImageTileKey& key) const {
    
    std::string file_path = GetTileFilePath(key);
    
    try {
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            return nullptr;
        }
        
        auto tile_data = std::make_unique<TileData>();
        tile_data->metadata.imagery_key = key;
        
        // Read data size
        std::uint64_t data_size;
        file.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));
        
        // Read actual data
        tile_data->data.resize(data_size);
        file.read(reinterpret_cast<char*>(tile_data->data.data()), data_size);
        
        if (!file.good()) {
            return nullptr;
        }
        
        tile_data->metadata.file_size = data_size;
        tile_data->loaded = true;
        
        return tile_data;
    } catch (const std::exception& e) {
        spdlog::error("Failed to load tile from disk: {}", e.what());
        return nullptr;
    }
}

bool BasicTileCache::SaveMetadataToDisk(const TileMetadata& metadata) const {
    const auto& key = metadata.imagery_key;
    std::string file_path = GetMetadataFilePath(key);
    
    try {
        std::filesystem::create_directories(std::filesystem::path(file_path).parent_path());
        std::ofstream file(file_path, std::ios::binary);
        if (!file) {
            return false;
        }
        
        // Write metadata fields
        file.write(reinterpret_cast<const char*>(&metadata.file_size), 
                   sizeof(metadata.file_size));
        
        // Write timestamps
        auto modified_time = std::chrono::system_clock::to_time_t(metadata.last_modified);
        auto expires_time = std::chrono::system_clock::to_time_t(metadata.expires_at);
        
        file.write(reinterpret_cast<const char*>(&modified_time), sizeof(modified_time));
        file.write(reinterpret_cast<const char*>(&expires_time), sizeof(expires_time));
        
        // Write etag length and data
        std::uint32_t etag_size = metadata.etag.size();
        file.write(reinterpret_cast<const char*>(&etag_size), sizeof(etag_size));
        file.write(metadata.etag.c_str(), etag_size);
        
        // Write other fields
        std::uint32_t content_type_size = metadata.content_type.size();
        file.write(reinterpret_cast<const char*>(&content_type_size), 
                   sizeof(content_type_size));
        file.write(metadata.content_type.c_str(), content_type_size);
        
        std::uint8_t compression_int = static_cast<std::uint8_t>(metadata.compression);
        file.write(reinterpret_cast<const char*>(&compression_int), sizeof(compression_int));
        
        file.write(reinterpret_cast<const char*>(&metadata.checksum), 
                   sizeof(metadata.checksum));
        file.write(reinterpret_cast<const char*>(&metadata.access_count), 
                   sizeof(metadata.access_count));
        
        auto access_time = std::chrono::system_clock::to_time_t(metadata.last_access);
        file.write(reinterpret_cast<const char*>(&access_time), sizeof(access_time));
        
        return file.good();
    } catch (const std::exception& e) {
        spdlog::error("Failed to save metadata to disk: {}", e.what());
        return false;
    }
}

std::unique_ptr<TileMetadata> BasicTileCache::LoadMetadataFromDisk(
    const imagery::ImageTileKey& key) const {
    
    std::string file_path = GetMetadataFilePath(key);
    
    try {
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            return nullptr;
        }
        
        auto metadata = std::make_unique<TileMetadata>();
        metadata->imagery_key = key;
        
        // Read metadata fields
        file.read(reinterpret_cast<char*>(&metadata->file_size), 
                  sizeof(metadata->file_size));
        
        // Read timestamps
        std::time_t modified_time, expires_time;
        file.read(reinterpret_cast<char*>(&modified_time), sizeof(modified_time));
        file.read(reinterpret_cast<char*>(&expires_time), sizeof(expires_time));
        
        metadata->last_modified = std::chrono::system_clock::from_time_t(modified_time);
        metadata->expires_at = std::chrono::system_clock::from_time_t(expires_time);
        
        // Read etag
        std::uint32_t etag_size;
        file.read(reinterpret_cast<char*>(&etag_size), sizeof(etag_size));
        metadata->etag.resize(etag_size);
        file.read(&metadata->etag[0], etag_size);
        
        // Read other fields
        std::uint32_t content_type_size;
        file.read(reinterpret_cast<char*>(&content_type_size), sizeof(content_type_size));
        metadata->content_type.resize(content_type_size);
        file.read(&metadata->content_type[0], content_type_size);
        
        std::uint8_t compression_int;
        file.read(reinterpret_cast<char*>(&compression_int), sizeof(compression_int));
        metadata->compression = static_cast<TileMetadata::Compression>(compression_int);
        
        file.read(reinterpret_cast<char*>(&metadata->checksum), 
                  sizeof(metadata->checksum));
        file.read(reinterpret_cast<char*>(&metadata->access_count), 
                  sizeof(metadata->access_count));
        
        std::time_t access_time;
        file.read(reinterpret_cast<char*>(&access_time), sizeof(access_time));
        metadata->last_access = std::chrono::system_clock::from_time_t(access_time);
        
        return file.good() ? std::move(metadata) : nullptr;
    } catch (const std::exception& e) {
        spdlog::error("Failed to load metadata from disk: {}", e.what());
        return nullptr;
    }
}

void BasicTileCache::UpdateLRU(const imagery::ImageTileKey& key) const {
    auto it = lru_index_.find(key);
    
    if (it != lru_index_.end()) {
        // Move to end (most recently used)
        std::size_t old_index = it->second;
        lru_list_.erase(lru_list_.begin() + old_index);
        lru_list_.push_back(key);
        
        // Update indices
        for (std::size_t i = old_index; i < lru_list_.size(); ++i) {
            lru_index_[lru_list_[i]] = i;
        }
        lru_index_[key] = lru_list_.size() - 1;
    } else {
        // Add to end
        lru_list_.push_back(key);
        lru_index_[key] = lru_list_.size() - 1;
    }
}

void BasicTileCache::EvictFromMemory(std::size_t required_space) {
    std::size_t freed_space = 0;
    
    while (freed_space < required_space && !lru_list_.empty()) {
        // Remove least recently used (front of list)
        imagery::ImageTileKey key_to_evict = lru_list_.front();
        lru_list_.erase(lru_list_.begin());
        lru_index_.erase(key_to_evict);
        
        // Update indices
        for (auto& [coords, index] : lru_index_) {
            if (index > 0) {
                --index;
            }
        }
        
        // Remove from memory cache
        auto it = memory_cache_.find(key_to_evict);
        if (it != memory_cache_.end()) {
            freed_space += it->second->GetDataSize();
            stats_.memory_cache_size -= it->second->GetDataSize();
            stats_.memory_cache_count--;
            stats_.total_evictions++;
            
            memory_cache_.erase(it);
        }
    }
}

void BasicTileCache::EvictFromDisk(std::size_t required_space) {
    (void)required_space;  // Suppress unused parameter warning
    // TODO: Implement disk eviction logic
    // This would involve analyzing file sizes and removing oldest/largest files
    spdlog::warn("Disk eviction not yet implemented");
}

bool BasicTileCache::IsTileExpired(const TileMetadata& metadata) const {
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(
        now - metadata.last_modified).count();
    
    return static_cast<std::uint64_t>(age) > config_.tile_ttl;
}

std::size_t BasicTileCache::CalculateCurrentMemoryUsage() const {
    std::size_t total_size = 0;
    for (const auto& [coords, tile_data] : memory_cache_) {
        if (tile_data) {
            total_size += tile_data->GetDataSize();
        }
    }
    return total_size;
}

std::size_t BasicTileCache::CalculateCurrentDiskUsage() const {
    std::size_t total_size = 0;
    
    try {
        for (const auto& entry : 
             std::filesystem::recursive_directory_iterator(config_.disk_cache_directory)) {
            if (entry.is_regular_file()) {
                total_size += entry.file_size();
            }
        }
    } catch (const std::exception&) {
        // Ignore directory iteration errors
    }
    
    return total_size;
}

} // namespace earth_map
