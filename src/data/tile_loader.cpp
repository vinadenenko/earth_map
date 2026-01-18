/**
 * @file tile_loader.cpp
 * @brief Tile loading system implementation with libcurl support and thread pool
 */

#include <earth_map/data/tile_loader.h>
#include <earth_map/math/tile_mathematics.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <random>
#include <thread>
#include <future>
#include <set>
#include <unordered_set>
#include <regex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <curl/curl.h>
#include <spdlog/spdlog.h>

namespace earth_map {

// Predefined tile providers
namespace TileProviders {
    
const TileProvider OpenStreetMap = {
    .name = "OpenStreetMap",
    .url_template = "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    .subdomains = "",
    .min_zoom = 0,
    .max_zoom = 19,
    .format = "png",
    .attribution = "© OpenStreetMap contributors",
    .user_agent = "EarthMap/1.0",
    .api_key = "",
    .custom_headers = {}
};

const TileProvider OpenStreetMapHumanitarian = {
    .name = "OpenStreetMapHumanitarian",
    .url_template = "https://tile.openstreetmap.fr/hot/{z}/{x}/{y}.png",
    .subdomains = "abc",
    .min_zoom = 0,
    .max_zoom = 19,
    .format = "png",
    .attribution = "© OpenStreetMap contributors, Tiles style by Humanitarian OpenStreetMap Team",
    .user_agent = "EarthMap/1.0",
    .api_key = "",
    .custom_headers = {}
};

const TileProvider StamenTerrain = {
    .name = "StamenTerrain",
    .url_template = "https://stamen-tiles-{s}.a.ssl.fastly.net/terrain/{z}/{x}/{y}.png",
    .subdomains = "abcd",
    .min_zoom = 0,
    .max_zoom = 18,
    .format = "png",
    .attribution = "Map tiles by Stamen Design, under CC BY 3.0. Data by OpenStreetMap, under ODbL",
    .user_agent = "EarthMap/1.0",
    .api_key = "",
    .custom_headers = {}
};

const TileProvider StamenWatercolor = {
    .name = "StamenWatercolor",
    .url_template = "https://stamen-tiles-{s}.a.ssl.fastly.net/watercolor/{z}/{x}/{y}.jpg",
    .subdomains = "abcd",
    .min_zoom = 0,
    .max_zoom = 18,
    .format = "jpg",
    .attribution = "Map tiles by Stamen Design, under CC BY 3.0. Data by OpenStreetMap, under CC BY SA",
    .user_agent = "EarthMap/1.0",
    .api_key = "",
    .custom_headers = {}
};

const TileProvider CartoDBPositron = {
    .name = "CartoDBPositron",
    .url_template = "https://cartodb-basemaps-{s}.global.ssl.fastly.net/light_all/{z}/{x}/{y}.png",
    .subdomains = "abcd",
    .min_zoom = 0,
    .max_zoom = 18,
    .format = "png",
    .attribution = "© OpenStreetMap contributors © CARTO",
    .user_agent = "EarthMap/1.0",
    .api_key = "",
    .custom_headers = {}
};

const TileProvider CartoDBDarkMatter = {
    .name = "CartoDBDarkMatter",
    .url_template = "https://cartodb-basemaps-{s}.global.ssl.fastly.net/dark_all/{z}/{x}/{y}.png",
    .subdomains = "abcd",
    .min_zoom = 0,
    .max_zoom = 18,
    .format = "png",
    .attribution = "© OpenStreetMap contributors © CARTO",
    .user_agent = "EarthMap/1.0",
    .api_key = "",
    .custom_headers = {}
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
 * @brief Download task for thread pool
 */
struct DownloadTask {
    TileCoordinates coordinates;
    std::string provider_name;
    std::shared_ptr<std::promise<TileLoadResult>> promise;
    TileLoadCallback callback;
    
    DownloadTask(const TileCoordinates& coords, 
                const std::string& provider,
                std::shared_ptr<std::promise<TileLoadResult>> prom,
                TileLoadCallback cb)
        : coordinates(coords), provider_name(provider), promise(prom), callback(cb) {}
};

/**
 * @brief Thread pool for tile downloading
 */
class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads) : stop_(false) {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this] { 
                            return stop_ || !tasks_.empty(); 
                        });
                        
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    
                    task();
                }
            });
        }
    }
    
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        
        condition_.notify_all();
        
        for (auto& worker : workers_) {
            worker.join();
        }
    }
    
    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace(std::forward<F>(f));
        }
        
        condition_.notify_one();
    }
    
    std::size_t GetQueueSize() const {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }
    
    std::size_t GetThreadCount() const {
        return workers_.size();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};

/**
 * @brief libcurl write callback
 */
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::vector<uint8_t>* userp) {
    size_t total_size = size * nmemb;
    userp->insert(userp->end(), static_cast<uint8_t*>(contents), 
                  static_cast<uint8_t*>(contents) + total_size);
    return total_size;
}

/**
 * @brief Basic tile loader implementation with thread pool and libcurl
 */
class BasicTileLoader : public TileLoader {
public:
    explicit BasicTileLoader(const TileLoaderConfig& config) 
        : config_(config), thread_pool_(config_.max_concurrent_downloads) {
        // Initialize curl globally
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    
    ~BasicTileLoader() override {
        curl_global_cleanup();
    }
    
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
    std::string default_provider_{"OpenStreetMap"};
    TileLoaderStats stats_;
    ThreadPool thread_pool_;
    
    // Async loading state
    mutable std::mutex loading_mutex_;
    std::unordered_set<TileCoordinates, TileCoordinatesHash> loading_tiles_;
    std::map<TileCoordinates, std::shared_ptr<std::promise<TileLoadResult>>> active_loads_;
    
    // Internal methods
    TileLoadResult LoadTileInternal(const TileCoordinates& coordinates,
                                   const std::string& provider_name);
    bool DownloadTile(const std::string& url, 
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     std::vector<std::uint8_t>& data,
                     std::uint32_t& status_code);
    CURL* CreateCurlHandle() const;
    void SetupCurlHandle(CURL* curl, const std::string& url,
                        const std::vector<std::pair<std::string, std::string>>& headers,
                        std::vector<std::uint8_t>& data) const;
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
    
    // Thread pool is already initialized in constructor with the initial size
    
    // Add default providers
    AddProvider(TileProviders::OpenStreetMap);
    AddProvider(TileProviders::OpenStreetMapHumanitarian);
    AddProvider(TileProviders::StamenTerrain);
    AddProvider(TileProviders::CartoDBPositron);
    
    // Set default provider
    if (default_provider_.empty() && !providers_.empty()) {
        default_provider_ = providers_.begin()->first;
    }
    
    spdlog::info("Tile loader initialized with {} providers and {} threads", 
                providers_.size(), config_.max_concurrent_downloads);
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
    // Commented for now to prevent double increment in UpdateStats
    // stats_.total_requests++;
    
    // Check cache first
    // TODO: tile_cache_ is null now, investigate later
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
    
    // Enqueue download task
    thread_pool_.enqueue([this, coordinates, provider_name, callback, promise]() {
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
    });
    
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
    stats.queued_downloads = thread_pool_.GetQueueSize();
    
    return stats;
}

bool BasicTileLoader::SetConfiguration(const TileLoaderConfig& config) {
    if (config.max_concurrent_downloads != config_.max_concurrent_downloads) {
        // Note: Thread pool size is fixed after construction
        // In a real implementation, you might recreate the entire loader
        spdlog::warn("Thread pool size change requires recreating tile loader");
    }
    
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

CURL* BasicTileLoader::CreateCurlHandle() const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        spdlog::error("Failed to initialize curl handle");
        return nullptr;
    }
    
    // Set common options
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config_.user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config_.follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(config_.timeout));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config_.timeout));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, config_.verify_ssl ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, config_.verify_ssl ? 2L : 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // Prevent signals for thread safety
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // Enable all supported encodings
    
    if (config_.enable_http2) {
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
    }
    
    return curl;
}

void BasicTileLoader::SetupCurlHandle(CURL* curl, const std::string& url,
                                     const std::vector<std::pair<std::string, std::string>>& headers,
                                     std::vector<std::uint8_t>& data) const {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    
    // Set headers
    struct curl_slist* header_list = nullptr;
    for (const auto& [name, value] : headers) {
        std::string header_str = name + ": " + value;
        header_list = curl_slist_append(header_list, header_str.c_str());
    }
    
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }
    
    // Set proxy if configured
    if (!config_.proxy_url.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, config_.proxy_url.c_str());
        
        if (!config_.proxy_username.empty()) {
            curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, config_.proxy_username.c_str());
            curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, config_.proxy_password.c_str());
        }
    }
    
    // Cleanup will be handled by the caller
}

bool BasicTileLoader::DownloadTile(const std::string& url,
                                  const std::vector<std::pair<std::string, std::string>>& headers,
                                  std::vector<std::uint8_t>& data,
                                  std::uint32_t& status_code) {
    CURL* curl = CreateCurlHandle();
    if (!curl) {
        return false;
    }
    
    data.clear();
    
    SetupCurlHandle(curl, url, headers, data);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        status_code = static_cast<std::uint32_t>(response_code);
        
        // Check if response is successful (2xx status codes)
        if (status_code >= 200 && status_code < 300) {
            curl_easy_cleanup(curl);
            return true;
        } else {
            spdlog::warn("HTTP error {} for URL: {}", status_code, url);
        }
    } else {
        spdlog::error("Curl error: {} for URL: {}", curl_easy_strerror(res), url);
    }
    
    curl_easy_cleanup(curl);
    return false;
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
