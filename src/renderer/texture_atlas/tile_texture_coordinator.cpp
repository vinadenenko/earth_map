/**
 * @file tile_texture_coordinator.cpp
 * @brief Implementation of tile texture coordinator
 */

#include <earth_map/renderer/texture_atlas/tile_texture_coordinator.h>
#include <spdlog/spdlog.h>

#include <utility>

namespace earth_map {

TileTextureCoordinator::TileTextureCoordinator(
    std::shared_ptr<TileCache> cache,
    std::shared_ptr<TileLoader> loader,
    int num_worker_threads,
    bool skip_gl_init)
    : loader_(std::move(loader)) {
    if (!loader_) {
        spdlog::error("TileTextureCoordinator: null loader provided");
        throw std::invalid_argument("TileLoader cannot be null");
    }

    // TileLoader is the sole authority that resolves a render request into a
    // source-aware imagery cache key. Bind the cache once at that boundary.
    loader_->SetTileCache(cache);

    // Create upload queue (shared between workers and GL thread)
    upload_queue_ = std::make_shared<GLUploadQueue>();

    // Create tile texture pool (replaces atlas for tile rendering)
    tile_pool_ = std::make_unique<TileTexturePool>(
        kDefaultTileSize,
        kDefaultMaxPoolLayers,
        skip_gl_init
    );

    if (tile_pool_->GetMaxLayers() >= IndirectionTextureManager::kInvalidLayer) {
        throw std::invalid_argument(
            "Pool max_layers must be less than indirection sentinel value (0xFFFF)");
    }

    // Create indirection texture manager
    indirection_manager_ = std::make_unique<IndirectionTextureManager>(skip_gl_init);

    // Create worker pool
    worker_pool_ = std::make_unique<TileLoadWorkerPool>(
        loader_,
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

    // Step 2: Apply backpressure — skip if too many tiles are already pending.
    // Note: This check is outside the write lock, so multiple threads may pass
    // it simultaneously. The per-item check inside the lock (below) provides a
    // tighter bound. The overshoot is bounded by to_load.size() per thread.
    if (pending_load_count_.load() >= kMaxPendingLoads) {
        spdlog::debug("Backpressure: {} pending loads >= limit {}, dropping {} requests",
                      pending_load_count_.load(), kMaxPendingLoads, to_load.size());
        return;
    }

    // Step 3: Mark tiles as Loading and submit to worker pool (write lock)
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);

        for (const auto& coords : to_load) {
            if (pending_load_count_.load() >= kMaxPendingLoads) {
                break;
            }

            auto& state = tile_states_[coords];
            if (state.status == TileStatus::NotLoaded) {
                state.status = TileStatus::Loading;
                state.request_time = std::chrono::steady_clock::now();
                pending_load_count_.fetch_add(1);

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
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    auto it = tile_states_.find(coords);
    return it != tile_states_.end() && it->second.status == TileStatus::Loaded;
}

glm::vec4 TileTextureCoordinator::GetTileUV(const TileCoordinates& coords) const {
    // With texture arrays, each tile uses full [0,1] UV range.
    // Return (0,0,1,1) if loaded, (0,0,0,0) if not.
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    auto it = tile_states_.find(coords);
    if (it != tile_states_.end() && it->second.status == TileStatus::Loaded) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
}

std::uint32_t TileTextureCoordinator::GetTilePoolTextureID() const {
    return tile_pool_->GetTextureArrayID();
}

std::uint32_t TileTextureCoordinator::GetIndirectionTextureID(
    const imagery::ImageTileKey& imagery_key) const {
    return indirection_manager_->GetTextureID(imagery_key);
}

std::uint16_t TileTextureCoordinator::GetIndirectionLayer(
    const imagery::ImageTileKey& imagery_key) const {
    return indirection_manager_->GetTileLayer(imagery_key);
}

glm::ivec2 TileTextureCoordinator::GetIndirectionOffset(
    const imagery::ImageTileKey& imagery_key) const {
    return indirection_manager_->GetWindowOffset(imagery_key);
}

std::optional<IndirectionTextureManager::PageTableBinding>
TileTextureCoordinator::GetIndirectionPageTableBinding(
    const imagery::ImageTileKey& imagery_key) const {
    return indirection_manager_->GetPageTableBinding(imagery_key);
}

void TileTextureCoordinator::UpdateIndirectionWindowCenter(
    const imagery::ImageTileKey& center_tile) {
    if (!indirection_manager_->UpdateWindowCenter(center_tile)) {
        return;
    }

    // A page table is a windowed, derived GPU view of the physical pool. A
    // window shift can clear entries that remain resident in the pool; replay
    // every resident page that belongs to this table so revisiting an area
    // never requires a redundant download/upload to become drawable again.
    std::vector<std::pair<imagery::ImageTileKey, std::uint16_t>> resident_pages;
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex_);
        resident_pages.reserve(tile_states_.size());
        for (const auto& entry : tile_states_) {
            const TileState& state = entry.second;
            if (state.status != TileStatus::Loaded || !state.imagery_key.has_value() ||
                state.pool_layer < 0 ||
                state.imagery_key->imagery_source_id != center_tile.imagery_source_id ||
                state.imagery_key->matrix_set_id != center_tile.matrix_set_id ||
                state.imagery_key->address.level != center_tile.address.level) {
                continue;
            }

            const int current_layer = tile_pool_->GetLayerIndex(*state.imagery_key);
            if (current_layer == state.pool_layer) {
                resident_pages.emplace_back(
                    *state.imagery_key,
                    static_cast<std::uint16_t>(current_layer));
            }
        }
    }

    for (const auto& [imagery_key, layer] : resident_pages) {
        indirection_manager_->SetTileLayer(imagery_key, layer);
    }
}

std::optional<imagery::ImageTileKey> TileTextureCoordinator::ResolveImageryTileKey(
    const TileCoordinates& coords) const {
    return loader_->ResolveImageTileKey(coords);
}

int TileTextureCoordinator::GetTileLayerIndex(const TileCoordinates& coords) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    const auto it = tile_states_.find(coords);
    return it != tile_states_.end() && it->second.status == TileStatus::Loaded
        ? it->second.pool_layer
        : -1;
}

std::uint32_t TileTextureCoordinator::GetAtlasTextureID() const {
    return tile_pool_->GetTextureArrayID();
}

void TileTextureCoordinator::ProcessUploads(int max_uploads_per_frame) {
    if (max_uploads_per_frame <= 0) {
        return;
    }

    int processed_count = 0;
    for (int i = 0; i < max_uploads_per_frame; ++i) {
        auto cmd = upload_queue_->TryPop();
        if (!cmd) {
            break;
        }

        processed_count++;

        if (!cmd->imagery_key.has_value() || !cmd->imagery_key->IsValid()) {
            std::unique_lock<std::shared_mutex> lock(state_mutex_);
            auto it = tile_states_.find(cmd->coords);
            if (it != tile_states_.end() && it->second.status == TileStatus::Loading) {
                tile_states_.erase(it);
                pending_load_count_.fetch_sub(1);
            }
            spdlog::error("Rejected unkeyed imagery upload for tile {}", cmd->coords.GetKey());
            if (cmd->on_complete) {
                cmd->on_complete(cmd->coords);
            }
            continue;
        }

        // Upload to tile pool
        int layer = tile_pool_->UploadTile(
            *cmd->imagery_key,
            cmd->pixel_data.data(),
            cmd->width,
            cmd->height,
            cmd->channels
        );

        // Pool full — evict LRU tile and retry
        if (layer < 0 && tile_pool_->GetFreeLayers() == 0) {
            auto candidate = tile_pool_->GetEvictionCandidate();
            if (candidate.has_value()) {
                std::optional<TileCoordinates> candidate_coords;
                {
                    std::shared_lock<std::shared_mutex> lock(state_mutex_);
                    for (const auto& [coords, state] : tile_states_) {
                        if (state.status == TileStatus::Loaded &&
                            state.imagery_key.has_value() &&
                            *state.imagery_key == *candidate) {
                            candidate_coords = coords;
                            break;
                        }
                    }
                }

                if (candidate_coords.has_value()) {
                    indirection_manager_->ClearTile(*candidate);
                } else {
                    spdlog::error(
                        "Physical imagery page lost its legacy page-table owner before eviction: "
                        "{}/{}/{}/{}/{}",
                        candidate->imagery_source_id,
                        candidate->matrix_set_id,
                        candidate->address.level,
                        candidate->address.column,
                        candidate->address.row);
                }
                tile_pool_->EvictTile(*candidate);

                {
                    std::unique_lock<std::shared_mutex> lock(state_mutex_);
                    if (candidate_coords.has_value()) {
                        tile_states_.erase(*candidate_coords);
                    }
                }

                spdlog::debug(
                    "Evicted LRU imagery page {}/{}/{}/{}/{} to make room for {}",
                    candidate->imagery_source_id,
                    candidate->matrix_set_id,
                    candidate->address.level,
                    candidate->address.column,
                    candidate->address.row,
                    cmd->coords.GetKey());

                layer = tile_pool_->UploadTile(
                    *cmd->imagery_key,
                    cmd->pixel_data.data(),
                    cmd->width,
                    cmd->height,
                    cmd->channels
                );
            }
        }

        if (layer >= 0) {
            // A physical layer is useful only if the current GPU page-table
            // window can address it.  Treat an out-of-window completion as a
            // stale upload, not as a loaded tile: otherwise RequestTiles()
            // will never retry it after the camera moves back into range.
            const bool mapped = indirection_manager_->SetTileLayer(
                *cmd->imagery_key,
                static_cast<std::uint16_t>(layer));

            if (!mapped) {
                tile_pool_->EvictTile(*cmd->imagery_key);

                std::unique_lock<std::shared_mutex> lock(state_mutex_);
                auto it = tile_states_.find(cmd->coords);
                if (it != tile_states_.end() && it->second.status == TileStatus::Loading) {
                    tile_states_.erase(it);
                    pending_load_count_.fetch_sub(1);
                }

                spdlog::debug("Discarded stale tile upload outside current page-table window: {}",
                              cmd->coords.GetKey());
                if (cmd->on_complete) {
                    cmd->on_complete(cmd->coords);
                }
                continue;
            }

            // Update state to Loaded and decrement pending counter. If the
            // request was cancelled while its command was queued, do not leave
            // an unowned physical page or an indirection mapping behind.
            bool installed = false;
            {
                std::unique_lock<std::shared_mutex> lock(state_mutex_);
                auto it = tile_states_.find(cmd->coords);
                if (it != tile_states_.end() && it->second.status == TileStatus::Loading) {
                    it->second.status = TileStatus::Loaded;
                    it->second.pool_layer = layer;
                    it->second.imagery_key = *cmd->imagery_key;
                    pending_load_count_.fetch_sub(1);
                    installed = true;
                }
            }

            if (installed) {
                spdlog::trace("Tile {} uploaded to pool layer {}",
                              cmd->coords.GetKey(), layer);
            } else {
                indirection_manager_->ClearTile(*cmd->imagery_key);
                tile_pool_->EvictTile(*cmd->imagery_key);
                spdlog::debug("Discarded upload whose request state was removed: {}",
                              cmd->coords.GetKey());
            }
        } else {
            // Upload failed — remove from pending state
            std::unique_lock<std::shared_mutex> lock(state_mutex_);
            auto it = tile_states_.find(cmd->coords);
            if (it != tile_states_.end() && it->second.status == TileStatus::Loading) {
                tile_states_.erase(it);
                pending_load_count_.fetch_sub(1);
            }
            spdlog::warn("Failed to upload tile {} to pool", cmd->coords.GetKey());
        }

        if (cmd->on_complete) {
            cmd->on_complete(cmd->coords);
        }
    }
}

void TileTextureCoordinator::TouchTiles(const std::vector<TileCoordinates>& tiles) {
    for (const TileCoordinates& coords : tiles) {
        std::shared_lock<std::shared_mutex> lock(state_mutex_);
        const auto it = tile_states_.find(coords);
        if (it != tile_states_.end() && it->second.status == TileStatus::Loaded &&
            it->second.imagery_key.has_value()) {
            tile_pool_->TouchTile(*it->second.imagery_key);
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
                // Use the pool's last-used timestamp (updated by TouchTile)
                // rather than request_time, so actively rendered tiles survive.
                if (!state.imagery_key.has_value()) {
                    continue;
                }
                const auto last_used = tile_pool_->GetLastUsedTime(*state.imagery_key);
                const auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - last_used);

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

    std::size_t evicted = 0;
    for (const auto& coords : to_evict) {
        // Re-check state — may have changed between lock upgrade
        auto it = tile_states_.find(coords);
        if (it == tile_states_.end() || it->second.status != TileStatus::Loaded) {
            continue;
        }

        // Clear from indirection texture
        if (it->second.imagery_key.has_value()) {
            indirection_manager_->ClearTile(*it->second.imagery_key);
        }

        // Evict from tile pool
        if (it->second.imagery_key.has_value()) {
            tile_pool_->EvictTile(*it->second.imagery_key);
        }

        // Remove from state map
        tile_states_.erase(it);
        ++evicted;

        spdlog::debug("Evicted old tile {}", coords.GetKey());
    }

    return evicted;
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
