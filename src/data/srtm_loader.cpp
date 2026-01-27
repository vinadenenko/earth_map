// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/data/srtm_loader.h>
#include <earth_map/data/hgt_parser.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <thread>

namespace earth_map {

namespace {

/// Thread pool for async loading
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads) : stop_(false) {
        for (size_t i = 0; i < num_threads; ++i) {
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
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    template <class F>
    void Enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace(std::forward<F>(f));
        }

        condition_.notify_one();
    }

    size_t GetQueueSize() const {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};

/// libcurl write callback
size_t WriteCallback(void* contents, size_t size, size_t nmemb,
                     std::vector<uint8_t>* userp) {
    const size_t total_size = size * nmemb;
    userp->insert(userp->end(), static_cast<uint8_t*>(contents),
                  static_cast<uint8_t*>(contents) + total_size);
    return total_size;
}

} // anonymous namespace

/// Basic SRTM loader implementation
class BasicSRTMLoader : public SRTMLoader {
public:
    explicit BasicSRTMLoader(const SRTMLoaderConfig& config)
        : config_(config), thread_pool_(config_.max_concurrent_downloads) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~BasicSRTMLoader() override {
        CancelAllLoads();
        curl_global_cleanup();
    }

    bool Initialize(const SRTMLoaderConfig& config) override {
        config_ = config;

        // Create local directory if using local disk source
        if (config_.source == SRTMSource::LOCAL_DISK) {
            try {
                std::filesystem::create_directories(config_.local_directory);
            } catch (...) {
                return false;
            }
        }

        return true;
    }

    SRTMLoadResult LoadTile(const SRTMCoordinates& coordinates) override {
        const auto start_time = std::chrono::high_resolution_clock::now();

        SRTMLoadResult result;
        result.coordinates = coordinates;

        // Validate coordinates
        if (!coordinates.IsValid()) {
            result.error_message = "Invalid coordinates";
            ++stats_.tiles_failed;
            return result;
        }

        // Try to load from local disk first
        if (config_.source == SRTMSource::LOCAL_DISK) {
            result = LoadFromDisk(coordinates);
        } else if (config_.source == SRTMSource::HTTP) {
            result = LoadFromHTTP(coordinates);
        }

        // Calculate load time
        const auto end_time = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        result.load_time_ms = static_cast<double>(duration.count());

        // Update statistics
        if (result.success) {
            ++stats_.tiles_loaded;
            UpdateAverageLoadTime(result.load_time_ms);
        } else {
            ++stats_.tiles_failed;
        }

        return result;
    }

    std::future<SRTMLoadResult> LoadTileAsync(
        const SRTMCoordinates& coordinates,
        SRTMLoadCallback callback) override {

        auto promise = std::make_shared<std::promise<SRTMLoadResult>>();
        auto future = promise->get_future();

        // Track pending load
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_loads_.insert(coordinates);
            ++stats_.pending_loads;
        }

        // Enqueue task
        thread_pool_.Enqueue([this, coordinates, callback, promise]() {
            SRTMLoadResult result = LoadTile(coordinates);

            // Remove from pending
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                pending_loads_.erase(coordinates);
                if (stats_.pending_loads > 0) {
                    --stats_.pending_loads;
                }
            }

            // Call callback if provided
            if (callback) {
                callback(result);
            }

            // Set promise value
            promise->set_value(result);
        });

        return future;
    }

    std::vector<std::future<SRTMLoadResult>> LoadTilesAsync(
        const std::vector<SRTMCoordinates>& coordinates,
        SRTMLoadCallback callback) override {

        std::vector<std::future<SRTMLoadResult>> futures;
        futures.reserve(coordinates.size());

        for (const auto& coords : coordinates) {
            futures.push_back(LoadTileAsync(coords, callback));
        }

        return futures;
    }

    bool CancelLoad(const SRTMCoordinates& coordinates) override {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        return pending_loads_.erase(coordinates) > 0;
    }

    void CancelAllLoads() override {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_loads_.clear();
        stats_.pending_loads = 0;
    }

    SRTMLoaderStats GetStatistics() const override {
        return stats_;
    }

    SRTMLoaderConfig GetConfiguration() const override {
        return config_;
    }

    bool SetConfiguration(const SRTMLoaderConfig& config) override {
        config_ = config;
        return true;
    }

    bool IsLoading(const SRTMCoordinates& coordinates) const override {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        return pending_loads_.find(coordinates) != pending_loads_.end();
    }

    size_t GetPendingLoadCount() const override {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        return pending_loads_.size();
    }

private:
    SRTMLoadResult LoadFromDisk(const SRTMCoordinates& coordinates) {
        SRTMLoadResult result;
        result.coordinates = coordinates;

        // Try preferred resolution first
        std::string filename = FormatSRTMFilename(coordinates);
        std::string filepath = config_.local_directory + "/" + filename;

        // Check if file exists
        if (!std::filesystem::exists(filepath)) {
            result.error_message = "File not found: " + filepath;
            return result;
        }

        // Parse file
        result.tile_data = HGTParser::ParseFile(filepath);
        if (!result.tile_data) {
            result.error_message = "Failed to parse HGT file";
            return result;
        }

        result.success = true;
        result.file_size_bytes = result.tile_data->GetMetadata().file_size;
        ++stats_.cache_hits;

        return result;
    }

    SRTMLoadResult LoadFromHTTP(const SRTMCoordinates& coordinates) {
        SRTMLoadResult result;
        result.coordinates = coordinates;

        // Build URL from template
        std::string url = BuildURL(coordinates);
        if (url.empty()) {
            result.error_message = "Invalid URL template";
            return result;
        }

        // Download with retries
        std::vector<uint8_t> data;
        bool downloaded = false;

        for (uint32_t retry = 0; retry <= config_.max_retries; ++retry) {
            if (DownloadFile(url, data)) {
                downloaded = true;
                break;
            }

            if (retry < config_.max_retries) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        if (!downloaded) {
            result.error_message = "Failed to download from URL: " + url;
            return result;
        }

        // Parse data
        result.tile_data = HGTParser::Parse(data, coordinates);
        if (!result.tile_data) {
            result.error_message = "Failed to parse downloaded HGT data";
            return result;
        }

        result.success = true;
        result.file_size_bytes = data.size();
        stats_.bytes_downloaded += data.size();
        ++stats_.cache_misses;

        return result;
    }

    std::string BuildURL(const SRTMCoordinates& coordinates) const {
        if (config_.url_template.empty()) {
            return "";
        }

        std::string url = config_.url_template;

        // Replace {lat} placeholder
        std::ostringstream lat_str;
        lat_str << (coordinates.latitude >= 0 ? 'N' : 'S')
                << std::abs(coordinates.latitude);
        size_t pos = url.find("{lat}");
        if (pos != std::string::npos) {
            url.replace(pos, 5, lat_str.str());
        }

        // Replace {lon} placeholder
        std::ostringstream lon_str;
        lon_str << (coordinates.longitude >= 0 ? 'E' : 'W')
                << std::abs(coordinates.longitude);
        pos = url.find("{lon}");
        if (pos != std::string::npos) {
            url.replace(pos, 5, lon_str.str());
        }

        return url;
    }

    bool DownloadFile(const std::string& url, std::vector<uint8_t>& data) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            return false;
        }

        data.clear();

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.timeout_seconds);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, config_.user_agent.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        const CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        return (res == CURLE_OK && !data.empty());
    }

    void UpdateAverageLoadTime(double load_time_ms) {
        const uint64_t total = stats_.tiles_loaded;
        if (total == 0) {
            stats_.average_load_time_ms = load_time_ms;
        } else {
            stats_.average_load_time_ms =
                (stats_.average_load_time_ms * (total - 1) + load_time_ms) / total;
        }
    }

    SRTMLoaderConfig config_;
    ThreadPool thread_pool_;
    SRTMLoaderStats stats_;

    mutable std::mutex pending_mutex_;
    std::set<SRTMCoordinates> pending_loads_;
};

// Custom comparator for SRTMCoordinates in std::set
bool operator<(const SRTMCoordinates& lhs, const SRTMCoordinates& rhs) {
    if (lhs.latitude != rhs.latitude) {
        return lhs.latitude < rhs.latitude;
    }
    return lhs.longitude < rhs.longitude;
}

std::unique_ptr<SRTMLoader> SRTMLoader::Create(const SRTMLoaderConfig& config) {
    auto loader = std::make_unique<BasicSRTMLoader>(config);
    if (!loader->Initialize(config)) {
        return nullptr;
    }
    return loader;
}

} // namespace earth_map
