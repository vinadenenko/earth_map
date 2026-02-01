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

    // Create tile texture pool (replaces atlas for tile rendering)
    tile_pool_ = std::make_unique<TileTexturePool>(
        256,   // tile_size
        512,   // max_layers
        skip_gl_init
    );

    // Create indirection texture manager
    indirection_manager_ = std::make_unique<IndirectionTextureManager>(skip_gl_init);

    // Create worker pool
    worker_pool_ = std::make_unique<TileLoadWorkerPool>(
        cache,
        loader,
        upload_queue_,
        num_worker_threads
    );

    spdlog::info("TileTextureCoordinator initialized with {} workers (tile pool + indirection)",
                 num_worker_threads);
}

TileTextureCoordinator::~TileTextureCoordinator() {
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

            if (it == tile_states_.end() ||
                it->second.status == TileStatus::NotLoaded) {
                to_load.push_back(coords);
            }
        }
    }

    if (to_load.empty()) {
        return;
    }

    // Step 2: Mark tiles as Loading and submit to worker pool (write lock)
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);

        for (const auto& coords : to_load) {
            auto& state = tile_states_[coords];
            if (state.status == TileStatus::NotLoaded) {
                state.status = TileStatus::Loading;
                state.request_time = std::chrono::steady_clock::now();

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
    return tile_pool_->IsTileLoaded(coords);
}

glm::vec4 TileTextureCoordinator::GetTileUV(const TileCoordinates& coords) const {
    // With texture arrays, each tile uses full [0,1] UV range.
    // Return (0,0,1,1) if loaded, (0,0,0,0) if not.
    if (tile_pool_->IsTileLoaded(coords)) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
}

std::uint32_t TileTextureCoordinator::GetTilePoolTextureID() const {
    return tile_pool_->GetTextureArrayID();
}

std::uint32_t TileTextureCoordinator::GetIndirectionTextureID(int zoom) const {
    return indirection_manager_->GetTextureID(zoom);
}

glm::ivec2 TileTextureCoordinator::GetIndirectionOffset(int zoom) const {
    return indirection_manager_->GetWindowOffset(zoom);
}

void TileTextureCoordinator::UpdateIndirectionWindowCenter(
    int zoom, int center_tile_x, int center_tile_y) {
    indirection_manager_->UpdateWindowCenter(zoom, center_tile_x, center_tile_y);
}

int TileTextureCoordinator::GetTileLayerIndex(const TileCoordinates& coords) const {
    return tile_pool_->GetLayerIndex(coords);
}

std::uint32_t TileTextureCoordinator::GetAtlasTextureID() const {
    return tile_pool_->GetTextureArrayID();
}

void TileTextureCoordinator::ProcessUploads(int max_uploads_per_frame) {
    if (max_uploads_per_frame <= 0) {
        return;
    }

    for (int i = 0; i < max_uploads_per_frame; ++i) {
        auto cmd = upload_queue_->TryPop();
        if (!cmd) {
            break;
        }

        // Upload to tile pool
        int layer = tile_pool_->UploadTile(
            cmd->coords,
            cmd->pixel_data.data(),
            cmd->width,
            cmd->height,
            cmd->channels
        );

        if (layer >= 0) {
            // Update indirection texture
            indirection_manager_->SetTileLayer(
                cmd->coords,
                static_cast<std::uint16_t>(layer));

            // Update state to Loaded
            std::unique_lock<std::shared_mutex> lock(state_mutex_);

            auto it = tile_states_.find(cmd->coords);
            if (it != tile_states_.end()) {
                it->second.status = TileStatus::Loaded;
                it->second.pool_layer = layer;

                spdlog::trace("Tile {} uploaded to pool layer {}",
                             cmd->coords.GetKey(), layer);
            }
        } else {
            spdlog::warn("Failed to upload tile {} to pool", cmd->coords.GetKey());
        }

        if (cmd->on_complete) {
            cmd->on_complete(cmd->coords);
        }
    }
}

std::size_t TileTextureCoordinator::EvictUnusedTiles(std::chrono::seconds max_age) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<TileCoordinates> to_evict;

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

    std::unique_lock<std::shared_mutex> lock(state_mutex_);

    for (const auto& coords : to_evict) {
        // Clear from indirection texture
        indirection_manager_->ClearTile(coords);

        // Evict from tile pool
        tile_pool_->EvictTile(coords);

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
    spdlog::trace("Tile {} load complete, queued for upload", coords.GetKey());
}

} // namespace earth_map
