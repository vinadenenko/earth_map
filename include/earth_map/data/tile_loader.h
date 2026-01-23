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
 * @brief Authentication types for tile providers
 */
enum class AuthType {
    NONE,       ///< No authentication
    API_KEY,    ///< API key in URL
    BEARER,     ///< Bearer token
    BASIC       ///< Basic authentication
};

/**
 * @brief Abstract base class for tile providers
 */
class TileProvider {
public:
    TileProvider() = default;
    virtual ~TileProvider() = default;

    /**
     * @brief Build URL for specific tile
     */
    virtual std::string BuildTileURL(const TileCoordinates& coords) const = 0;

    /**
     * @brief Get headers for tile request
     */
    virtual std::vector<std::pair<std::string, std::string>> GetHeaders() const { return {}; }

    /**
     * @brief Get attribution text
     */
    virtual std::string GetAttribution() const { return ""; }

    /**
     * @brief Get provider name
     */
    virtual std::string GetName() const { return ""; }

    /**
     * @brief Get minimum zoom level
     */
    virtual std::int32_t GetMinZoom() const { return 0; }

    /**
     * @brief Get maximum zoom level
     */
    virtual std::int32_t GetMaxZoom() const { return 18; }

    /**
     * @brief Get image format
     */
    virtual std::string GetFormat() const { return "png"; }

    /**
     * @brief Get user agent
     */
    virtual std::string GetUserAgent() const { return "EarthMap/1.0"; }

    /**
     * @brief Get timeout
     */
    virtual std::uint32_t GetTimeout() const { return 30; }

    /**
     * @brief Get max retries
     */
    virtual std::uint32_t GetMaxRetries() const { return 3; }

    /**
     * @brief Get retry delay
     */
    virtual std::uint32_t GetRetryDelay() const { return 1000; }
};

/**
 * @brief Basic XYZ tile provider for simple URL templates
 */
class BasicXYZTileProvider : public TileProvider {
public:
    BasicXYZTileProvider(const std::string& name,
                        const std::string& url_template,
                        const std::string& subdomains = "",
                        std::int32_t min_zoom = 0,
                        std::int32_t max_zoom = 18,
                        const std::string& format = "png",
                        const std::vector<std::pair<std::string, std::string>>& custom_headers = {},
                        AuthType auth_type = AuthType::NONE,
                        const std::string& api_key = "",
                        const std::string& user_agent = "EarthMap/1.0");

    std::string BuildTileURL(const TileCoordinates& coords) const override;
    std::vector<std::pair<std::string, std::string>> GetHeaders() const override;
    std::string GetAttribution() const override;
    std::int32_t GetMinZoom() const override;
    std::int32_t GetMaxZoom() const override;
    std::string GetFormat() const override;
    std::string GetName() const override;

private:
    std::string name_;
    std::string url_template_;
    std::string subdomains_;
    std::int32_t min_zoom_;
    std::int32_t max_zoom_;
    std::string format_;
    std::vector<std::pair<std::string, std::string>> custom_headers_;
    AuthType auth_type_;
    std::string api_key_;
    std::string user_agent_;
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
    virtual bool AddProvider(std::shared_ptr<TileProvider> provider) = 0;
    
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

extern std::shared_ptr<TileProvider> OpenStreetMap;
extern std::shared_ptr<TileProvider> OpenStreetMapHumanitarian;
extern std::shared_ptr<TileProvider> StamenTerrain;
extern std::shared_ptr<TileProvider> CartoDBPositron;

} // namespace TileProviders

} // namespace earth_map