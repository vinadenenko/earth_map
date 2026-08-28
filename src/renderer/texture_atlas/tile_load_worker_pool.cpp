/**
 * @file tile_load_worker_pool.cpp
 * @brief Implementation of tile load worker pool
 */

#include <earth_map/renderer/texture_atlas/tile_load_worker_pool.h>
#include <spdlog/spdlog.h>
#include <array>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace earth_map {

TileLoadWorkerPool::TileLoadWorkerPool(
    std::shared_ptr<TileLoader> loader,
    std::shared_ptr<GLUploadQueue> upload_queue,
    int num_threads)
    : loader_(std::move(loader))
    , upload_queue_(std::move(upload_queue))
    , shutdown_flag_(false) {

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
    std::function<void(const TileCoordinates&)> on_complete,
    std::function<void(const TileCoordinates&)> on_discarded) {

    std::lock_guard<std::mutex> lock(queue_mutex_);

    // Check if tile is already queued or being processed (deduplication)
    if (in_flight_.find(coords) != in_flight_.end()) {
        spdlog::trace("Tile {} already queued or processing, skipping", coords.GetKey());
        return;
    }

    // Add to in-flight set
    in_flight_.insert(coords);

    // Add to priority queue
    request_queue_.emplace(
        coords, priority, std::move(on_complete), std::move(on_discarded));

    spdlog::trace("Submitted tile {} with priority {}", coords.GetKey(), priority);

    // Notify one worker
    queue_cv_.notify_one();
}

void TileLoadWorkerPool::ReclaimUploadCommands(
    std::vector<std::unique_ptr<GLUploadCommand>> commands) {
    if (commands.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (auto& command : commands) {
            if (command) {
                reclamation_queue_.push_back(std::move(command));
            }
        }
    }
    queue_cv_.notify_all();
}

std::vector<TileCoordinates> TileLoadWorkerPool::CancelQueuedRequestsExcept(
    const std::unordered_set<TileCoordinates, TileCoordinatesHash>& active_tiles) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    std::priority_queue<TileLoadRequest> retained;
    std::vector<TileCoordinates> cancelled;
    while (!request_queue_.empty()) {
        TileLoadRequest request = request_queue_.top();
        request_queue_.pop();
        if (active_tiles.contains(request.coords)) {
            retained.push(std::move(request));
        } else {
            in_flight_.erase(request.coords);
            cancelled.push_back(request.coords);
        }
    }
    request_queue_ = std::move(retained);
    return cancelled;
}

std::size_t TileLoadWorkerPool::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return request_queue_.size();
}

void TileLoadWorkerPool::WorkerThreadMain() {
    spdlog::debug("Worker thread started");

    bool reclaim_after_request = false;
    while (true) {
        TileLoadRequest request;
        std::unique_ptr<GLUploadCommand> reclaimed_command;
        bool have_request = false;

        // Wait for request or shutdown
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            queue_cv_.wait(lock, [this]() {
                return !request_queue_.empty() || !reclamation_queue_.empty() ||
                    shutdown_flag_.load();
            });

            // Service current imagery first, but release one retired payload
            // after each request so reclamation remains bounded under motion.
            if (!reclamation_queue_.empty() &&
                (request_queue_.empty() || reclaim_after_request)) {
                reclaimed_command = std::move(reclamation_queue_.front());
                reclamation_queue_.pop_front();
                reclaim_after_request = false;
            } else if (!request_queue_.empty()) {
                request = request_queue_.top();
                request_queue_.pop();
                have_request = true;
                reclaim_after_request = true;
            } else if (shutdown_flag_.load()) {
                break;
            }
        }

        // Let the local unique_ptr release outside every queue mutex and off
        // the render thread.
        if (reclaimed_command) {
            reclaimed_command.reset();
            continue;
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

    // The loader resolves the provider's canonical imagery key before checking
    // its cache. A render request only has TileCoordinates, which are not a
    // cache identity because the same address can belong to different sources
    // or matrix sets. Keep this worker downstream of that resolution.
    auto load_result = loader_->LoadTile(coords, "");  // Empty provider = default

    if (!load_result.success) {
        spdlog::warn("Failed to load tile {}: {}", coords.GetKey(), load_result.error_message);
        // Enqueue an empty command so ProcessUploads sees the failure and
        // resets the tile from Loading back to NotLoaded (via its existing
        // upload-failed path). Without this the tile stays Loading forever.
        if (!upload_queue_->Push(std::make_unique<GLUploadCommand>(coords)) &&
            request.on_discarded) {
            request.on_discarded(coords);
        }
        return;
    }

    if (!load_result.tile_data) {
        spdlog::warn("Loaded tile {} but data is null", coords.GetKey());
        if (!upload_queue_->Push(std::make_unique<GLUploadCommand>(coords)) &&
            request.on_discarded) {
            request.on_discarded(coords);
        }
        return;
    }

    if (!load_result.imagery_key.has_value() || !load_result.imagery_key->IsValid() ||
        load_result.tile_data->metadata.imagery_key != *load_result.imagery_key) {
        spdlog::error("Loaded tile {} without a consistent canonical imagery key", coords.GetKey());
        if (!upload_queue_->Push(std::make_unique<GLUploadCommand>(coords)) &&
            request.on_discarded) {
            request.on_discarded(coords);
        }
        return;
    }

    TileData tile_data = *load_result.tile_data;
    tile_data.loaded = true;

    // Step 3: Decode image data
    if (!DecodeImage(tile_data)) {
        spdlog::warn("Failed to decode image for tile {}", coords.GetKey());
        // Enqueue an empty command so ProcessUploads resets the tile state.
        if (!upload_queue_->Push(std::make_unique<GLUploadCommand>(coords)) &&
            request.on_discarded) {
            request.on_discarded(coords);
        }
        return;
    }

    // Step 4: Create GL upload command
    auto upload_cmd = std::make_unique<GLUploadCommand>();
    upload_cmd->coords = coords;
    upload_cmd->imagery_key = *load_result.imagery_key;
    upload_cmd->pixel_data = std::move(tile_data.data);
    upload_cmd->width = tile_data.width;
    upload_cmd->height = tile_data.height;
    upload_cmd->channels = tile_data.channels;

    // Step 5: Push to GL upload queue
    if (!upload_queue_->Push(std::move(upload_cmd))) {
        if (request.on_discarded) {
            request.on_discarded(coords);
        }
        return;
    }

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
