#pragma once

/**
 * @file tile_loader.h
 * @brief Tile loading system for remote tile fetching and local storage
 * 
 * Provides HTTP client implementation for tile servers, support for multiple
 * tile providers, error handling, and asynchronous loading capabilities.
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/data/tile_cache.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <future>

namespace earth_map {

/**
 * @brief Tile provider configuration
 */
struct TileProvider {
    /** Provider name */
    std::string name;
    
    /** Base URL template (use {x}, {y}, {z} placeholders) */
    std::string url_template;
    
    /** Available subdomains for load balancing */
    std::string subdomains;
    
    /** Minimum zoom level */
    std::int32_t min_zoom = 0;
    
    /** Maximum zoom level */
    std::int32_t max_zoom = 18;
    
    /** Image format */
    std::string format = "png";
    
    /** Attribution text */
    std::string attribution;
    
    /** User agent string */
    std::string user_agent = "EarthMap/1.0";
    
    /** API key (if required) */
    std::string api_key;
    
    /** Authentication type */
    enum class AuthType {
        NONE,       ///< No authentication
        API_KEY,    ///< API key in URL
        BEARER,     ///< Bearer token
        BASIC       ///< Basic authentication
    } auth_type = AuthType::NONE;
    
    /** Custom headers */
    std::vector<std::pair<std::string, std::string>> custom_headers;
    
    /** Request timeout in seconds */
    std::uint32_t timeout = 30;
    
    /** Maximum retry attempts */
    std::uint32_t max_retries = 3;
    
    /** Retry delay in milliseconds */
    std::uint32_t retry_delay = 1000;
    
    /**
     * @brief Build URL for specific tile
     */
    std::string BuildTileURL(const TileCoordinates& coords) const;
    
    /**
     * @brief Get headers for tile request
     */
    std::vector<std::pair<std::string, std::string>> GetHeaders() const;
};

/**
 * @brief Load operation result
 */
struct TileLoadResult {
    /** Success status */
    bool success = false;
    
    /** HTTP status code */
    std::uint32_t status_code = 0;
    
    /** Error message (if failed) */
    std::string error_message;
    
    /** Loaded tile data */
    std::shared_ptr<TileData> tile_data;
    
    /** Load time in milliseconds */
    std::uint64_t load_time_ms = 0;
    
    /** Number of retry attempts */
    std::uint32_t retry_count = 0;
    
    /** Tile coordinates */
    TileCoordinates coordinates;
    
    /** Provider name */
    std::string provider_name;
};

/**
 * @brief Tile loader configuration
 */
struct TileLoaderConfig {
    /** Maximum concurrent downloads */
    std::size_t max_concurrent_downloads = 4;
    
    /** Request timeout in seconds */
    std::uint32_t timeout = 30;
    
    /** Maximum retry attempts */
    std::uint32_t max_retries = 3;
    
    /** Retry delay in milliseconds */
    std::uint32_t retry_delay = 1000;
    
    /** Enable HTTP/2 */
    bool enable_http2 = true;
    
    /** Enable compression */
    bool enable_compression = true;
    
    /** Follow redirects */
    bool follow_redirects = true;
    
    /** Verify SSL certificates */
    bool verify_ssl = true;
    
    /** Connection cache size */
    std::size_t connection_cache_size = 10;
    
    /** User agent string */
    std::string user_agent = "EarthMap/1.0";
    
    /** Custom CA certificate path */
    std::string ca_cert_path;
    
    /** Proxy settings */
    std::string proxy_url;
    
    /** Proxy username */
    std::string proxy_username;
    
    /** Proxy password */
    std::string proxy_password;
};

/**
 * @brief Tile loader statistics
 */
struct TileLoaderStats {
    /** Total requests */
    std::size_t total_requests = 0;
    
    /** Successful requests */
    std::size_t successful_requests = 0;
    
    /** Failed requests */
    std::size_t failed_requests = 0;
    
    /** Cached requests (served from cache) */
    std::size_t cached_requests = 0;
    
    /** Total bytes downloaded */
    std::uint64_t total_bytes_downloaded = 0;
    
    /** Average load time in milliseconds */
    float average_load_time_ms = 0.0f;
    
    /** Active download count */
    std::size_t active_downloads = 0;
    
    /** Queued download count */
    std::size_t queued_downloads = 0;
    
    /**
     * @brief Get success rate
     */
    float GetSuccessRate() const {
        return total_requests > 0 ? 
            static_cast<float>(successful_requests) / total_requests : 0.0f;
    }
    
    /**
     * @brief Get cache hit rate
     */
    float GetCacheHitRate() const {
        return total_requests > 0 ? 
            static_cast<float>(cached_requests) / total_requests : 0.0f;
    }
    
    /**
     * @brief Reset statistics
     */
    void Reset() {
        total_requests = 0;
        successful_requests = 0;
        failed_requests = 0;
        cached_requests = 0;
        total_bytes_downloaded = 0;
        average_load_time_ms = 0.0f;
        active_downloads = 0;
        queued_downloads = 0;
    }
};

/**
 * @brief Load request callback type
 */
using TileLoadCallback = std::function<void(const TileLoadResult&)>;

/**
 * @brief Tile loader interface
 * 
 * Provides HTTP-based tile loading from remote tile servers with
 * caching, retry logic, and asynchronous loading capabilities.
 */
class TileLoader {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~TileLoader() = default;
    
    /**
     * @brief Initialize the tile loader
     * 
     * @param config Configuration parameters
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize(const TileLoaderConfig& config) = 0;
    
    /**
     * @brief Set tile cache
     * 
     * @param cache Tile cache instance
     */
    virtual void SetTileCache(std::shared_ptr<TileCache> cache) = 0;
    
    /**
     * @brief Add tile provider
     * 
     * @param provider Tile provider configuration
     * @return true if provider was added, false otherwise
     */
    virtual bool AddProvider(const TileProvider& provider) = 0;
    
    /**
     * @brief Remove tile provider
     * 
     * @param name Provider name
     * @return true if provider was removed, false otherwise
     */
    virtual bool RemoveProvider(const std::string& name) = 0;
    
    /**
     * @brief Get tile provider
     * 
     * @param name Provider name
     * @return const TileProvider* Provider pointer or nullptr if not found
     */
    virtual const TileProvider* GetProvider(const std::string& name) const = 0;
    
    /**
     * @brief Get all provider names
     * 
     * @return std::vector<std::string> List of provider names
     */
    virtual std::vector<std::string> GetProviderNames() const = 0;
    
    /**
     * @brief Load tile synchronously
     * 
     * @param coordinates Tile coordinates
     * @param provider_name Provider name (optional, uses default if empty)
     * @return TileLoadResult Load result
     */
    virtual TileLoadResult LoadTile(const TileCoordinates& coordinates,
                                   const std::string& provider_name = "") = 0;
    
    /**
     * @brief Load tile asynchronously
     * 
     * @param coordinates Tile coordinates
     * @param callback Load completion callback
     * @param provider_name Provider name (optional, uses default if empty)
     * @return std::future<TileLoadResult> Future for load result
     */
    virtual std::future<TileLoadResult> LoadTileAsync(
        const TileCoordinates& coordinates,
        TileLoadCallback callback = nullptr,
        const std::string& provider_name = "") = 0;
    
    /**
     * @brief Load multiple tiles asynchronously
     * 
     * @param coordinates List of tile coordinates
     * @param callback Completion callback for each tile
     * @param provider_name Provider name (optional, uses default if empty)
     * @return std::vector<std::future<TileLoadResult>> Futures for load results
     */
    virtual std::vector<std::future<TileLoadResult>> LoadTilesAsync(
        const std::vector<TileCoordinates>& coordinates,
        TileLoadCallback callback = nullptr,
        const std::string& provider_name = "") = 0;
    
    /**
     * @brief Cancel tile loading
     * 
     * @param coordinates Tile coordinates to cancel
     * @return true if cancellation was successful, false otherwise
     */
    virtual bool CancelLoad(const TileCoordinates& coordinates) = 0;
    
    /**
     * @brief Cancel all pending loads
     */
    virtual void CancelAllLoads() = 0;
    
    /**
     * @brief Get loader statistics
     * 
     * @return TileLoaderStats Current statistics
     */
    virtual TileLoaderStats GetStatistics() const = 0;
    
    /**
     * @brief Get configuration
     * 
     * @return TileLoaderConfig Current configuration
     */
    virtual TileLoaderConfig GetConfiguration() const = 0;
    
    /**
     * @brief Set configuration
     * 
     * @param config New configuration
     * @return true if configuration was applied, false otherwise
     */
    virtual bool SetConfiguration(const TileLoaderConfig& config) = 0;
    
    /**
     * @brief Check if tile is being loaded
     * 
     * @param coordinates Tile coordinates
     * @return true if tile is currently loading, false otherwise
     */
    virtual bool IsLoading(const TileCoordinates& coordinates) const = 0;
    
    /**
     * @brief Get currently loading tiles
     * 
     * @return std::vector<TileCoordinates> List of loading tile coordinates
     */
    virtual std::vector<TileCoordinates> GetLoadingTiles() const = 0;
    
    /**
     * @brief Preload tiles into cache
     * 
     * @param coordinates List of tile coordinates to preload
     * @param provider_name Provider name (optional, uses default if empty)
     * @return std::size_t Number of tiles successfully preloaded
     */
    virtual std::size_t PreloadTiles(const std::vector<TileCoordinates>& coordinates,
                                    const std::string& provider_name = "") = 0;
    
    /**
     * @brief Set default provider
     * 
     * @param name Provider name
     * @return true if provider was set as default, false otherwise
     */
    virtual bool SetDefaultProvider(const std::string& name) = 0;
    
    /**
     * @brief Get default provider
     * 
     * @return std::string Default provider name
     */
    virtual std::string GetDefaultProvider() const = 0;

protected:
    /**
     * @brief Protected constructor
     */
    TileLoader() = default;
};

/**
 * @brief Factory function to create tile loader
 * 
 * @param config Configuration parameters
 * @return std::unique_ptr<TileLoader> New tile loader instance
 */
std::unique_ptr<TileLoader> CreateTileLoader(const TileLoaderConfig& config = {});

/**
 * @brief Predefined tile providers
 */
namespace TileProviders {
    /**
     * @brief OpenStreetMap standard tiles
     */
    extern const TileProvider OpenStreetMap;
    
    /**
     * @brief OpenStreetMap humanitarian tiles
     */
    extern const TileProvider OpenStreetMapHumanitarian;
    
    /**
     * @brief Stamen terrain tiles
     */
    extern const TileProvider StamenTerrain;
    
    /**
     * @brief Stamen watercolor tiles
     */
    extern const TileProvider StamenWatercolor;
    
    /**
     * @brief CartoDB positron tiles
     */
    extern const TileProvider CartoDBPositron;
    
    /**
     * @brief CartoDB dark matter tiles
     */
    extern const TileProvider CartoDBDarkMatter;
}

} // namespace earth_map