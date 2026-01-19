/**
 * @file tile_texture_coordinator.cpp
 * @brief Implementation of tile texture coordinator
 */

#include <earth_map/renderer/texture_atlas/tile_texture_coordinator.h>
#include <spdlog/spdlog.h>

namespace earth_map {

TileTextureCoordinator::TileTextureCoordinator(
    std::shared_ptr<TileCache> cache,
    std::shared_ptr<TileLoader> loader,
    int num_worker_threads,
    bool skip_gl_init)
{
    if (!loader) {
        spdlog::error("TileTextureCoordinator: null loader provided");
        throw std::invalid_argument("TileLoader cannot be null");
    }

    // Create upload queue (shared between workers and GL thread)
    upload_queue_ = std::make_shared<GLUploadQueue>();

    // Create atlas manager (GL thread only)
    atlas_manager_ = std::make_unique<TextureAtlasManager>(
        2048,  // atlas_width
        2048,  // atlas_height
        256,   // tile_size
        skip_gl_init
    );

    // Create worker pool
    worker_pool_ = std::make_unique<TileLoadWorkerPool>(
        cache,
        loader,
        upload_queue_,
        num_worker_threads
    );

    spdlog::info("TileTextureCoordinator initialized with {} workers",
                 num_worker_threads);
}

TileTextureCoordinator::~TileTextureCoordinator() {
    // Worker pool destructor will gracefully shutdown workers
    spdlog::info("TileTextureCoordinator shutting down");
}

void TileTextureCoordinator::RequestTiles(
    const std::vector<TileCoordinates>& tiles,
    int priority)
{
    if (tiles.empty()) {
        return;
    }

    // Step 1: Find tiles that need loading (read lock)
    std::vector<TileCoordinates> to_load;
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex_);

        for (const auto& coords : tiles) {
            auto it = tile_states_.find(coords);

            // Load if: not in map, or status is NotLoaded
            if (it == tile_states_.end() ||
                it->second.status == TileStatus::NotLoaded) {
                to_load.push_back(coords);
            }
        }
    }

    if (to_load.empty()) {
        return;  // All tiles already loaded or loading
    }

    // Step 2: Mark tiles as Loading and submit to worker pool (write lock)
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);

        for (const auto& coords : to_load) {
            // Check again under write lock (TOCTOU prevention)
            auto& state = tile_states_[coords];
            if (state.status == TileStatus::NotLoaded) {
                state.status = TileStatus::Loading;
                state.request_time = std::chrono::steady_clock::now();

                // Submit to worker pool
                worker_pool_->SubmitRequest(coords, priority,
                    [this](const TileCoordinates& loaded_coords) {
                        this->OnTileLoadComplete(loaded_coords);
                    });

                spdlog::trace("Requested tile {}", coords.GetKey());
            }
        }
    }
}

bool TileTextureCoordinator::IsTileReady(const TileCoordinates& coords) const {
    // Check both state map and atlas (atlas is source of truth)
    // Tiles may be evicted from atlas without state map update
    return atlas_manager_->IsTileLoaded(coords);
}

glm::vec4 TileTextureCoordinator::GetTileUV(const TileCoordinates& coords) const {
    // Atlas manager is source of truth - it returns default UV if tile not loaded
    return atlas_manager_->GetTileUV(coords);
}

std::uint32_t TileTextureCoordinator::GetAtlasTextureID() const {
    return atlas_manager_->GetAtlasTextureID();
}

void TileTextureCoordinator::ProcessUploads(int max_uploads_per_frame) {
    if (max_uploads_per_frame <= 0) {
        return;
    }

    for (int i = 0; i < max_uploads_per_frame; ++i) {
        // Pop command from upload queue
        auto cmd = upload_queue_->TryPop();
        if (!cmd) {
            break;  // Queue empty
        }

        // Upload to atlas (GL calls happen here)
        int slot = atlas_manager_->UploadTile(
            cmd->coords,
            cmd->pixel_data.data(),
            cmd->width,
            cmd->height,
            cmd->channels
        );

        if (slot >= 0) {
            // Update state to Loaded
            std::unique_lock<std::shared_mutex> lock(state_mutex_);

            auto it = tile_states_.find(cmd->coords);
            if (it != tile_states_.end()) {
                it->second.status = TileStatus::Loaded;
                it->second.atlas_slot = slot;

                spdlog::trace("Tile {} uploaded to atlas slot {}",
                             cmd->coords.GetKey(), slot);
            }
        } else {
            spdlog::warn("Failed to upload tile {} to atlas", cmd->coords.GetKey());
        }

        // Execute completion callback if provided
        if (cmd->on_complete) {
            cmd->on_complete(cmd->coords);
        }
    }
}

std::size_t TileTextureCoordinator::EvictUnusedTiles(std::chrono::seconds max_age) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<TileCoordinates> to_evict;

    // Step 1: Find old tiles (read lock)
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex_);

        for (const auto& [coords, state] : tile_states_) {
            if (state.status == TileStatus::Loaded) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - state.request_time);

                if (age > max_age) {
                    to_evict.push_back(coords);
                }
            }
        }
    }

    if (to_evict.empty()) {
        return 0;
    }

    // Step 2: Evict tiles (write lock + atlas modification)
    std::unique_lock<std::shared_mutex> lock(state_mutex_);

    for (const auto& coords : to_evict) {
        // Evict from atlas
        atlas_manager_->EvictTile(coords);

        // Remove from state map
        tile_states_.erase(coords);

        spdlog::debug("Evicted old tile {}", coords.GetKey());
    }

    return to_evict.size();
}

TileTextureCoordinator::TileStatus
TileTextureCoordinator::GetTileStatus(const TileCoordinates& coords) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);

    auto it = tile_states_.find(coords);
    if (it == tile_states_.end()) {
        return TileStatus::NotLoaded;
    }

    return it->second.status;
}

void TileTextureCoordinator::OnTileLoadComplete(const TileCoordinates& coords) {
    // This callback is invoked by worker threads after loading and queuing
    // The tile is now in the upload queue, waiting for GL thread to process
    spdlog::trace("Tile {} load complete, queued for upload", coords.GetKey());

    // Note: We don't transition to Loaded here - that happens in ProcessUploads
    // after actual GL upload succeeds
}

} // namespace earth_map
