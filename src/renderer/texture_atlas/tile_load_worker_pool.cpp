/**
 * @file tile_load_worker_pool.cpp
 * @brief Implementation of tile load worker pool
 */

#include <earth_map/renderer/texture_atlas/tile_load_worker_pool.h>
#include <spdlog/spdlog.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace earth_map {

TileLoadWorkerPool::TileLoadWorkerPool(
    std::shared_ptr<TileCache> cache,
    std::shared_ptr<TileLoader> loader,
    std::shared_ptr<GLUploadQueue> upload_queue,
    int num_threads)
    : cache_(std::move(cache))
    , loader_(std::move(loader))
    , upload_queue_(std::move(upload_queue))
    , shutdown_flag_(false) {

    if (!cache_) {
        spdlog::warn("TileLoadWorkerPool: null cache provided");
    }
    if (!loader_) {
        spdlog::error("TileLoadWorkerPool: null loader provided");
        throw std::invalid_argument("TileLoader cannot be null");
    }
    if (!upload_queue_) {
        spdlog::error("TileLoadWorkerPool: null upload_queue provided");
        throw std::invalid_argument("GLUploadQueue cannot be null");
    }

    if (num_threads < 1) {
        num_threads = 1;
        spdlog::warn("TileLoadWorkerPool: num_threads < 1, defaulting to 1");
    }

    // Start worker threads
    workers_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&TileLoadWorkerPool::WorkerThreadMain, this);
    }

    spdlog::info("TileLoadWorkerPool started with {} worker threads", num_threads);
}

TileLoadWorkerPool::~TileLoadWorkerPool() {
    Shutdown();
}

void TileLoadWorkerPool::Shutdown() {
    // Signal shutdown
    shutdown_flag_.store(true);

    // Wake all workers
    queue_cv_.notify_all();

    // Wait for all workers to finish
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    spdlog::info("TileLoadWorkerPool shutdown complete");
}

void TileLoadWorkerPool::SubmitRequest(
    const TileCoordinates& coords,
    int priority,
    std::function<void(const TileCoordinates&)> on_complete) {

    std::lock_guard<std::mutex> lock(queue_mutex_);

    // Check if tile is already queued or being processed (deduplication)
    if (in_flight_.find(coords) != in_flight_.end()) {
        spdlog::trace("Tile {} already queued or processing, skipping", coords.GetKey());
        return;
    }

    // Add to in-flight set
    in_flight_.insert(coords);

    // Add to priority queue
    request_queue_.emplace(coords, priority, std::move(on_complete));

    spdlog::trace("Submitted tile {} with priority {}", coords.GetKey(), priority);

    // Notify one worker
    queue_cv_.notify_one();
}

std::size_t TileLoadWorkerPool::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return request_queue_.size();
}

void TileLoadWorkerPool::WorkerThreadMain() {
    spdlog::debug("Worker thread started");

    while (true) {
        TileLoadRequest request;
        bool have_request = false;

        // Wait for request or shutdown
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            queue_cv_.wait(lock, [this]() {
                return !request_queue_.empty() || shutdown_flag_.load();
            });

            // Get request from queue
            if (!request_queue_.empty()) {
                request = request_queue_.top();
                request_queue_.pop();
                have_request = true;
            } else if (shutdown_flag_.load()) {
                // Queue is empty and shutdown requested - exit
                break;
            }
        }

        // Process request outside of lock
        if (have_request) {
            ProcessRequest(request);

            // Remove from in-flight set
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                in_flight_.erase(request.coords);
            }
        }
    }

    spdlog::debug("Worker thread exiting");
}

void TileLoadWorkerPool::ProcessRequest(const TileLoadRequest& request) {
    const auto& coords = request.coords;
    spdlog::trace("Processing tile load request: {}", coords.GetKey());

    // Step 1: Check cache
    std::optional<TileData> cached_data;
    bool cache_hit = false;

    if (cache_) {
        cached_data = cache_->Get(coords);
        cache_hit = cached_data.has_value();
        if (cache_hit) {
            spdlog::trace("Cache hit for tile {}", coords.GetKey());
        }
    }

    TileData tile_data;
    if (cache_hit) {
        tile_data = std::move(*cached_data);
    } else {
        tile_data.metadata.coordinates = coords;
    }

    // Step 2: Load from network if cache miss
    if (!cache_hit) {
        spdlog::trace("Cache miss for tile {}, loading from network", coords.GetKey());

        // Use loader to download tile
        auto load_result = loader_->LoadTile(coords, "");  // Empty provider = default

        if (!load_result.success) {
            spdlog::warn("Failed to load tile {}: {}", coords.GetKey(), load_result.error_message);
            return;
        }

        if (!load_result.tile_data) {
            spdlog::warn("Loaded tile {} but data is null", coords.GetKey());
            return;
        }
        tile_data = *load_result.tile_data;

        // TODO: hardcoded 'loaded', basically we are loaded, but it is weird
        // Think about another loading indication
        // E.g. separated from disk/network loading, because now we have 'bool success' (http status) and 'bool loaded'
        tile_data.loaded = true;

        // Put in cache for future use
        if (cache_ && tile_data.loaded) {
            cache_->Put(tile_data);
        }
    }

    // Step 3: Decode image data
    if (!DecodeImage(tile_data)) {
        spdlog::warn("Failed to decode image for tile {}", coords.GetKey());
        return;
    }

    // Step 4: Create GL upload command
    auto upload_cmd = std::make_unique<GLUploadCommand>();
    upload_cmd->coords = coords;
    upload_cmd->pixel_data = std::move(tile_data.data);
    upload_cmd->width = tile_data.width;
    upload_cmd->height = tile_data.height;
    upload_cmd->channels = tile_data.channels;

    // Step 5: Push to GL upload queue
    upload_queue_->Push(std::move(upload_cmd));

    // Step 6: Execute callback if provided
    if (request.on_complete) {
        request.on_complete(coords);
    }

    spdlog::trace("Tile {} loaded, decoded, and queued for upload", coords.GetKey());
}

bool TileLoadWorkerPool::DecodeImage(TileData& tile_data) {
    // If image is already decoded (width/height set), skip
    if (tile_data.width > 0 && tile_data.height > 0) {
        spdlog::trace("Image already decoded ({}x{})", tile_data.width, tile_data.height);
        return true;
    }

    if (tile_data.data.empty()) {
        spdlog::warn("Cannot decode image: no data");
        return false;
    }

    // Use stb_image to decode
    int width = 0;
    int height = 0;
    int channels = 0;

    // Decode image, forcing RGBA (4 channels) for GL_RGBA8 texture pool compatibility
    constexpr int kDesiredChannels = 4;
    unsigned char* decoded_data = stbi_load_from_memory(
        tile_data.data.data(),
        static_cast<int>(tile_data.data.size()),
        &width,
        &height,
        &channels,
        kDesiredChannels
    );

    if (!decoded_data) {
        const char* error = stbi_failure_reason();
        spdlog::warn("stb_image decode failed: {}", error ? error : "unknown error");
        return false;
    }

    // stbi returns original channel count in 'channels' even when forcing,
    // so override to the actual output channel count
    channels = kDesiredChannels;

    // Update tile_data with decoded info
    tile_data.width = static_cast<std::uint32_t>(width);
    tile_data.height = static_cast<std::uint32_t>(height);
    tile_data.channels = static_cast<std::uint8_t>(channels);

    // Copy decoded data to tile_data
    const std::size_t decoded_size = width * height * channels;
    tile_data.data.assign(decoded_data, decoded_data + decoded_size);

    // Free stb_image memory
    stbi_image_free(decoded_data);

    spdlog::trace("Decoded image: {}x{}, {} channels, {} bytes",
                  width, height, channels, decoded_size);

    return true;
}

} // namespace earth_map
