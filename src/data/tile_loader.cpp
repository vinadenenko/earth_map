/**
 * @file tile_loader.cpp
 * @brief Tile loading system implementation
 */

#include <earth_map/data/tile_loader.h>
#include <earth_map/data/tile_cache.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <regex>
#include <spdlog/spdlog.h>

// Note: This is a simplified implementation without actual HTTP client
// In a real implementation, you would use libcurl or similar

namespace earth_map {

// Predefined tile providers
namespace TileProviders {
    
const TileProvider OpenStreetMap = {
    .name = "OpenStreetMap",
    .url_template = "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    .subdomains = "abc",
    .min_zoom = 0,
    .max_zoom = 19,
    .format = "png",
    .attribution = "© OpenStreetMap contributors",
    .user_agent = "EarthMap/1.0"
};

const TileProvider OpenStreetMapHumanitarian = {
    .name = "OpenStreetMapHumanitarian",
    .url_template = "https://tile.openstreetmap.fr/hot/{z}/{x}/{y}.png",
    .subdomains = "abc",
    .min_zoom = 0,
    .max_zoom = 19,
    .format = "png",
    .attribution = "© OpenStreetMap contributors, Tiles style by Humanitarian OpenStreetMap Team",
    .user_agent = "EarthMap/1.0"
};

const TileProvider StamenTerrain = {
    .name = "StamenTerrain",
    .url_template = "https://stamen-tiles-{s}.a.ssl.fastly.net/terrain/{z}/{x}/{y}.png",
    .subdomains = "abcd",
    .min_zoom = 0,
    .max_zoom = 18,
    .format = "png",
    .attribution = "Map tiles by Stamen Design, under CC BY 3.0. Data by OpenStreetMap, under ODbL",
    .user_agent = "EarthMap/1.0"
};

const TileProvider StamenWatercolor = {
    .name = "StamenWatercolor",
    .url_template = "https://stamen-tiles-{s}.a.ssl.fastly.net/watercolor/{z}/{x}/{y}.jpg",
    .subdomains = "abcd",
    .min_zoom = 0,
    .max_zoom = 18,
    .format = "jpg",
    .attribution = "Map tiles by Stamen Design, under CC BY 3.0. Data by OpenStreetMap, under CC BY SA",
    .user_agent = "EarthMap/1.0"
};

const TileProvider CartoDBPositron = {
    .name = "CartoDBPositron",
    .url_template = "https://cartodb-basemaps-{s}.global.ssl.fastly.net/light_all/{z}/{x}/{y}.png",
    .subdomains = "abcd",
    .min_zoom = 0,
    .max_zoom = 18,
    .format = "png",
    .attribution = "© OpenStreetMap contributors © CARTO",
    .user_agent = "EarthMap/1.0"
};

const TileProvider CartoDBDarkMatter = {
    .name = "CartoDBDarkMatter",
    .url_template = "https://cartodb-basemaps-{s}.global.ssl.fastly.net/dark_all/{z}/{x}/{y}.png",
    .subdomains = "abcd",
    .min_zoom = 0,
    .max_zoom = 18,
    .format = "png",
    .attribution = "© OpenStreetMap contributors © CARTO",
    .user_agent = "EarthMap/1.0"
};

} // namespace TileProviders

// TileProvider implementation
std::string TileProvider::BuildTileURL(const TileCoordinates& coords) const {
    std::string url = url_template;
    
    // Replace placeholders
    url = std::regex_replace(url, std::regex("\\{x\\}"), std::to_string(coords.x));
    url = std::regex_replace(url, std::regex("\\{y\\}"), std::to_string(coords.y));
    url = std::regex_replace(url, std::regex("\\{z\\}"), std::to_string(coords.zoom));
    
    // Replace subdomain placeholder
    if (!subdomains.empty()) {
        char subdomain = TileMathematics::GetTileSubdomain(coords, subdomains);
        url = std::regex_replace(url, std::regex("\\{s\\}"), std::string(1, subdomain));
    }
    
    // Add API key if needed
    if (auth_type == AuthType::API_KEY && !api_key.empty()) {
        url += "?api_key=" + api_key;
    }
    
    return url;
}

std::vector<std::pair<std::string, std::string>> TileProvider::GetHeaders() const {
    std::vector<std::pair<std::string, std::string>> headers;
    
    if (!user_agent.empty()) {
        headers.emplace_back("User-Agent", user_agent);
    }
    
    // Add authentication headers
    switch (auth_type) {
        case AuthType::BEARER:
            if (!api_key.empty()) {
                headers.emplace_back("Authorization", "Bearer " + api_key);
            }
            break;
        case AuthType::BASIC:
            if (!api_key.empty()) {
                headers.emplace_back("Authorization", "Basic " + api_key);
            }
            break;
        case AuthType::API_KEY:
        case AuthType::NONE:
        default:
            break;
    }
    
    // Add custom headers
    headers.insert(headers.end(), custom_headers.begin(), custom_headers.end());
    
    return headers;
}

/**
 * @brief Basic tile loader implementation
 */
class BasicTileLoader : public TileLoader {
public:
    explicit BasicTileLoader(const TileLoaderConfig& config) : config_(config) {}
    ~BasicTileLoader() override = default;
    
    bool Initialize(const TileLoaderConfig& config) override;
    void SetTileCache(std::shared_ptr<TileCache> cache) override;
    
    bool AddProvider(const TileProvider& provider) override;
    bool RemoveProvider(const std::string& name) override;
    const TileProvider* GetProvider(const std::string& name) const override;
    std::vector<std::string> GetProviderNames() const override;
    
    TileLoadResult LoadTile(const TileCoordinates& coordinates,
                           const std::string& provider_name = "") override;
    
    std::future<TileLoadResult> LoadTileAsync(
        const TileCoordinates& coordinates,
        TileLoadCallback callback = nullptr,
        const std::string& provider_name = "") override;
    
    std::vector<std::future<TileLoadResult>> LoadTilesAsync(
        const std::vector<TileCoordinates>& coordinates,
        TileLoadCallback callback = nullptr,
        const std::string& provider_name = "") override;
    
    bool CancelLoad(const TileCoordinates& coordinates) override;
    void CancelAllLoads() override;
    
    TileLoaderStats GetStatistics() const override;
    
    TileLoaderConfig GetConfiguration() const override { return config_; }
    bool SetConfiguration(const TileLoaderConfig& config) override;
    
    bool IsLoading(const TileCoordinates& coordinates) const override;
    std::vector<TileCoordinates> GetLoadingTiles() const override;
    
    std::size_t PreloadTiles(const std::vector<TileCoordinates>& coordinates,
                            const std::string& provider_name = "") override;
    
    bool SetDefaultProvider(const std::string& name) override;
    std::string GetDefaultProvider() const override;

private:
    TileLoaderConfig config_;
    std::shared_ptr<TileCache> tile_cache_;
    std::map<std::string, TileProvider> providers_;
    std::string default_provider_;
    TileLoaderStats stats_;
    
    // Async loading state
    mutable std::mutex loading_mutex_;
    std::set<TileCoordinates> loading_tiles_;
    std::map<TileCoordinates, std::shared_ptr<std::promise<TileLoadResult>>> active_loads_;
    
    // Internal methods
    TileLoadResult LoadTileInternal(const TileCoordinates& coordinates,
                                   const std::string& provider_name);
    bool DownloadTile(const std::string& url, 
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     std::vector<std::uint8_t>& data,
                     std::uint32_t& status_code);
    std::uint64_t GetCurrentTimeMs() const;
    void UpdateStats(const TileLoadResult& result);
    TileCoordinatesHash hasher_;
};

// Factory function
std::unique_ptr<TileLoader> CreateTileLoader(const TileLoaderConfig& config) {
    return std::make_unique<BasicTileLoader>(config);
}

bool BasicTileLoader::Initialize(const TileLoaderConfig& config) {
    config_ = config;
    
    // Add default providers
    AddProvider(TileProviders::OpenStreetMap);
    AddProvider(TileProviders::OpenStreetMapHumanitarian);
    AddProvider(TileProviders::StamenTerrain);
    AddProvider(TileProviders::CartoDBPositron);
    
    // Set default provider
    if (default_provider_.empty() && !providers_.empty()) {
        default_provider_ = providers_.begin()->first;
    }
    
    spdlog::info("Tile loader initialized with {} providers", providers_.size());
    return true;
}

void BasicTileLoader::SetTileCache(std::shared_ptr<TileCache> cache) {
    tile_cache_ = cache;
}

bool BasicTileLoader::AddProvider(const TileProvider& provider) {
    if (provider.name.empty()) {
        spdlog::warn("Attempted to add provider with empty name");
        return false;
    }
    
    providers_[provider.name] = provider;
    
    if (default_provider_.empty()) {
        default_provider_ = provider.name;
    }
    
    spdlog::info("Added tile provider: {}", provider.name);
    return true;
}

bool BasicTileLoader::RemoveProvider(const std::string& name) {
    auto it = providers_.find(name);
    if (it != providers_.end()) {
        providers_.erase(it);
        
        // Update default provider if needed
        if (default_provider_ == name) {
            default_provider_ = providers_.empty() ? "" : providers_.begin()->first;
        }
        
        spdlog::info("Removed tile provider: {}", name);
        return true;
    }
    
    return false;
}

const TileProvider* BasicTileLoader::GetProvider(const std::string& name) const {
    auto it = providers_.find(name.empty() ? default_provider_ : name);
    return (it != providers_.end()) ? &it->second : nullptr;
}

std::vector<std::string> BasicTileLoader::GetProviderNames() const {
    std::vector<std::string> names;
    for (const auto& [name, provider] : providers_) {
        names.push_back(name);
    }
    return names;
}

TileLoadResult BasicTileLoader::LoadTile(const TileCoordinates& coordinates,
                                        const std::string& provider_name) {
    stats_.total_requests++;
    
    // Check cache first
    if (tile_cache_) {
        auto cached_tile = tile_cache_->Retrieve(coordinates);
        if (cached_tile && cached_tile->IsValid()) {
            stats_.cached_requests++;
            
            TileLoadResult result;
            result.success = true;
            result.tile_data = cached_tile;
            result.coordinates = coordinates;
            result.provider_name = provider_name.empty() ? default_provider_ : provider_name;
            
            return result;
        }
    }
    
    return LoadTileInternal(coordinates, provider_name);
}

std::future<TileLoadResult> BasicTileLoader::LoadTileAsync(
    const TileCoordinates& coordinates,
    TileLoadCallback callback,
    const std::string& provider_name) {
    
    auto promise = std::make_shared<std::promise<TileLoadResult>>();
    auto future = promise->get_future();
    
    // Check if already loading
    {
        std::lock_guard<std::mutex> lock(loading_mutex_);
        if (loading_tiles_.find(coordinates) != loading_tiles_.end()) {
            // Already loading, return existing future
            auto it = active_loads_.find(coordinates);
            if (it != active_loads_.end()) {
                return it->second->get_future();
            }
        }
        
        loading_tiles_.insert(coordinates);
        active_loads_[coordinates] = promise;
    }
    
    // Launch async loading
    std::thread([this, coordinates, provider_name, callback, promise]() {
        auto result = LoadTile(coordinates, provider_name);
        
        if (callback) {
            callback(result);
        }
        
        promise->set_value(result);
        
        // Clean up loading state
        {
            std::lock_guard<std::mutex> lock(loading_mutex_);
            loading_tiles_.erase(coordinates);
            active_loads_.erase(coordinates);
        }
    }).detach();
    
    return future;
}

std::vector<std::future<TileLoadResult>> BasicTileLoader::LoadTilesAsync(
    const std::vector<TileCoordinates>& coordinates,
    TileLoadCallback callback,
    const std::string& provider_name) {
    
    std::vector<std::future<TileLoadResult>> futures;
    
    for (const auto& coords : coordinates) {
        futures.push_back(LoadTileAsync(coords, callback, provider_name));
    }
    
    return futures;
}

bool BasicTileLoader::CancelLoad(const TileCoordinates& coordinates) {
    std::lock_guard<std::mutex> lock(loading_mutex_);
    
    auto it = active_loads_.find(coordinates);
    if (it != active_loads_.end()) {
        // Set cancelled result
        TileLoadResult result;
        result.success = false;
        result.error_message = "Load cancelled";
        result.coordinates = coordinates;
        
        it->second->set_value(result);
        
        active_loads_.erase(it);
        loading_tiles_.erase(coordinates);
        
        return true;
    }
    
    return false;
}

void BasicTileLoader::CancelAllLoads() {
    std::lock_guard<std::mutex> lock(loading_mutex_);
    
    for (auto& [coords, promise] : active_loads_) {
        TileLoadResult result;
        result.success = false;
        result.error_message = "Load cancelled";
        result.coordinates = coords;
        
        promise->set_value(result);
    }
    
    active_loads_.clear();
    loading_tiles_.clear();
}

TileLoaderStats BasicTileLoader::GetStatistics() const {
    std::lock_guard<std::mutex> lock(loading_mutex_);
    
    TileLoaderStats stats = stats_;
    stats.active_downloads = loading_tiles_.size();
    
    return stats;
}

bool BasicTileLoader::SetConfiguration(const TileLoaderConfig& config) {
    config_ = config;
    return true;
}

bool BasicTileLoader::IsLoading(const TileCoordinates& coordinates) const {
    std::lock_guard<std::mutex> lock(loading_mutex_);
    return loading_tiles_.find(coordinates) != loading_tiles_.end();
}

std::vector<TileCoordinates> BasicTileLoader::GetLoadingTiles() const {
    std::lock_guard<std::mutex> lock(loading_mutex_);
    
    std::vector<TileCoordinates> tiles;
    for (const auto& coords : loading_tiles_) {
        tiles.push_back(coords);
    }
    
    return tiles;
}

std::size_t BasicTileLoader::PreloadTiles(const std::vector<TileCoordinates>& coordinates,
                                          const std::string& provider_name) {
    std::size_t preloaded_count = 0;
    
    for (const auto& coords : coordinates) {
        if (tile_cache_ && !tile_cache_->Contains(coords)) {
            auto result = LoadTile(coords, provider_name);
            if (result.success) {
                preloaded_count++;
            }
        }
    }
    
    if (preloaded_count > 0) {
        spdlog::info("Preloaded {} tiles", preloaded_count);
    }
    
    return preloaded_count;
}

bool BasicTileLoader::SetDefaultProvider(const std::string& name) {
    if (providers_.find(name) != providers_.end()) {
        default_provider_ = name;
        spdlog::info("Set default provider to: {}", name);
        return true;
    }
    
    spdlog::warn("Failed to set default provider: {} (not found)", name);
    return false;
}

std::string BasicTileLoader::GetDefaultProvider() const {
    return default_provider_;
}

TileLoadResult BasicTileLoader::LoadTileInternal(const TileCoordinates& coordinates,
                                                const std::string& provider_name) {
    auto start_time = GetCurrentTimeMs();
    
    TileLoadResult result;
    result.coordinates = coordinates;
    result.provider_name = provider_name.empty() ? default_provider_ : provider_name;
    
    // Get provider
    const TileProvider* provider = GetProvider(result.provider_name);
    if (!provider) {
        result.error_message = "Provider not found: " + result.provider_name;
        UpdateStats(result);
        return result;
    }
    
    // Validate coordinates
    if (!TileValidator::IsValidTile(coordinates) || 
        coordinates.zoom < provider->min_zoom || 
        coordinates.zoom > provider->max_zoom) {
        result.error_message = "Invalid tile coordinates";
        UpdateStats(result);
        return result;
    }
    
    // Build URL
    std::string url = provider->BuildTileURL(coordinates);
    auto headers = provider->GetHeaders();
    
    // Download tile
    std::vector<std::uint8_t> data;
    std::uint32_t status_code;
    
    for (std::uint32_t attempt = 0; attempt <= provider->max_retries; ++attempt) {
        result.retry_count = attempt;
        
        if (DownloadTile(url, headers, data, status_code)) {
            result.status_code = status_code;
            break;
        }
        
        if (attempt < provider->max_retries) {
            spdlog::warn("Tile download failed, retrying ({}/{}): {}/{}/{}", 
                        attempt + 1, provider->max_retries,
                        coordinates.x, coordinates.y, coordinates.zoom);
            
            // Wait before retry
            std::this_thread::sleep_for(
                std::chrono::milliseconds(provider->retry_delay));
        }
    }
    
    if (data.empty()) {
        result.error_message = "Failed to download tile after " + 
                             std::to_string(result.retry_count) + " attempts";
        UpdateStats(result);
        return result;
    }
    
    // Create tile data
    auto tile_data = std::make_shared<TileData>();
    tile_data->metadata.coordinates = coordinates;
    tile_data->metadata.file_size = data.size();
    tile_data->metadata.last_modified = std::chrono::system_clock::now();
    tile_data->metadata.last_access = std::chrono::system_clock::now();
    tile_data->metadata.content_type = "image/" + provider->format;
    tile_data->metadata.checksum = 0; // TODO: Calculate actual checksum
    tile_data->data = std::move(data);
    
    // Store in cache
    if (tile_cache_) {
        tile_cache_->Store(*tile_data);
    }
    
    result.success = true;
    result.tile_data = tile_data;
    result.load_time_ms = GetCurrentTimeMs() - start_time;
    
    UpdateStats(result);
    
    spdlog::debug("Loaded tile {}/{}/{} in {}ms", 
                 coordinates.x, coordinates.y, coordinates.zoom, result.load_time_ms);
    
    return result;
}

bool BasicTileLoader::DownloadTile(const std::string& url,
                                  const std::vector<std::pair<std::string, std::string>>& headers,
                                  std::vector<std::uint8_t>& data,
                                  std::uint32_t& status_code) {
    // Simplified placeholder implementation
    // In a real implementation, you would use libcurl or similar HTTP client
    
    // For now, simulate a successful download with dummy data
    status_code = 200;
    
    // Create dummy image data (small PNG header + some data)
    data = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // PNG signature
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, // IHDR chunk start
        0x00, 0x00, 0x00, 0x01, // Width: 1
        0x00, 0x00, 0x00, 0x01, // Height: 1
        0x08, 0x02, 0x00, 0x00, 0x00, // Bit depth, color type, compression, filter, interlace
        0x90, 0x77, 0x53, 0xDE, // CRC
        0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, // IDAT chunk start
        0x08, 0x99, 0x01, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, // Compressed data
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 // IEND chunk
    };
    
    // Simulate network delay
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    return true;
}

std::uint64_t BasicTileLoader::GetCurrentTimeMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void BasicTileLoader::UpdateStats(const TileLoadResult& result) {
    stats_.total_requests++;
    
    if (result.success) {
        stats_.successful_requests++;
        stats_.total_bytes_downloaded += result.tile_data ? result.tile_data->data.size() : 0;
        
        // Update average load time
        float total_time = stats_.average_load_time_ms * (stats_.successful_requests - 1);
        stats_.average_load_time_ms = (total_time + result.load_time_ms) / stats_.successful_requests;
    } else {
        stats_.failed_requests++;
    }
}

} // namespace earth_map