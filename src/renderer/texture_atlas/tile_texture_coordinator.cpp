/**
 * @file tile_texture_coordinator.cpp
 * @brief Implementation of tile texture coordinator
 */

#include <earth_map/renderer/texture_atlas/tile_texture_coordinator.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <limits>
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

    // Create worker pool
    worker_pool_ = std::make_unique<TileLoadWorkerPool>(
        loader_,
        upload_queue_,
        num_worker_threads
    );

    spdlog::info("TileTextureCoordinator initialized with {} workers and a texture-array pool",
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
                    },
                    [this](const TileCoordinates& discarded_coords) {
                        this->OnTileLoadDiscarded(discarded_coords);
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

std::uint32_t TileTextureCoordinator::GetPoolOccupiedLayers() const {
    return static_cast<std::uint32_t>(tile_pool_->GetOccupiedLayers());
}

std::uint32_t TileTextureCoordinator::GetPoolMaxLayers() const {
    return tile_pool_->GetMaxLayers();
}

std::uint64_t TileTextureCoordinator::GetPoolBytesUsed() const {
    return tile_pool_->GetBytesUsed();
}

std::uint64_t TileTextureCoordinator::GetPoolBytesMax() const {
    return tile_pool_->GetBytesMax();
}

std::optional<imagery::ImageTileKey> TileTextureCoordinator::ResolveImageryTileKey(
    const TileCoordinates& coords) const {
    return loader_->ResolveImageTileKey(coords);
}

std::optional<imagery::ImageTileKey>
TileTextureCoordinator::GetDefaultImageryRootKey() const {
    return loader_->GetDefaultImageryRootKey();
}

std::optional<imagery::TileMatrixSet> TileTextureCoordinator::GetImageryTileMatrixSet(
    const imagery::ImageTileKey& imagery_key) const {
    return loader_->GetTileMatrixSet(imagery_key);
}

std::optional<std::uint16_t> TileTextureCoordinator::GetResidentImageryLayer(
    const imagery::ImageTileKey& imagery_key) const {
    if (!imagery_key.IsValid()) {
        return std::nullopt;
    }

    const int layer = tile_pool_->GetLayerIndex(imagery_key);
    if (layer < 0 || layer > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(layer);
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

TileTextureCoordinator::UploadProcessStats TileTextureCoordinator::ProcessUploads(
    int max_uploads_per_frame) {
    UploadProcessStats stats;
    stats.queue_depth_before = upload_queue_->Size();

    if (max_uploads_per_frame <= 0) {
        stats.queue_depth_after = upload_queue_->Size();
        return stats;
    }

    for (int i = 0; i < max_uploads_per_frame; ++i) {
        auto cmd = upload_queue_->TryPop();
        if (!cmd) {
            break;
        }

        const auto command_start = std::chrono::steady_clock::now();
        ++stats.commands_processed;
        if (cmd->enqueued_at != std::chrono::steady_clock::time_point{}) {
            const double queue_wait_ms = std::chrono::duration<double, std::milli>(
                command_start - cmd->enqueued_at).count();
            if (queue_wait_ms > stats.max_queue_wait_ms) {
                stats.max_queue_wait_ms = queue_wait_ms;
            }
        }
        const auto finish_command = [&stats, command_start]() {
            const double command_cpu_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - command_start).count();
            stats.total_command_cpu_ms += command_cpu_ms;
            if (command_cpu_ms > stats.max_command_cpu_ms) {
                stats.max_command_cpu_ms = command_cpu_ms;
            }
        };
        const auto record_stage = [](double& total_cpu_ms,
                                     double& max_cpu_ms,
                                     std::chrono::steady_clock::time_point start) {
            const double elapsed_cpu_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            total_cpu_ms += elapsed_cpu_ms;
            if (elapsed_cpu_ms > max_cpu_ms) {
                max_cpu_ms = elapsed_cpu_ms;
            }
        };

        // A worker can complete while the camera moves. The active-request
        // refresh normally removes such commands from GLUploadQueue, but this
        // check closes the race without issuing a GL upload for stale data.
        {
            std::shared_lock<std::shared_mutex> lock(state_mutex_);
            const auto state = tile_states_.find(cmd->coords);
            if (state == tile_states_.end() || state->second.status != TileStatus::Loading) {
                finish_command();
                continue;
            }
        }

        if (!cmd->imagery_key.has_value() || !cmd->imagery_key->IsValid()) {
            const auto state_start = std::chrono::steady_clock::now();
            std::unique_lock<std::shared_mutex> lock(state_mutex_);
            auto it = tile_states_.find(cmd->coords);
            if (it != tile_states_.end() && it->second.status == TileStatus::Loading) {
                tile_states_.erase(it);
                pending_load_count_.fetch_sub(1);
            }
            record_stage(stats.total_residency_state_cpu_ms,
                         stats.max_residency_state_cpu_ms,
                         state_start);
            spdlog::error("Rejected unkeyed imagery upload for tile {}", cmd->coords.GetKey());
            if (cmd->on_complete) {
                cmd->on_complete(cmd->coords);
            }
            finish_command();
            continue;
        }

        const auto upload_to_pool = [&]() {
            const auto upload_start = std::chrono::steady_clock::now();
            ++stats.tile_pool_upload_attempts;
            stats.tile_pool_upload_attempt_bytes += cmd->pixel_data.size();
            const int result = tile_pool_->UploadTile(
                *cmd->imagery_key,
                cmd->pixel_data.data(),
                cmd->width,
                cmd->height,
                cmd->channels);
            const double upload_cpu_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - upload_start).count();
            stats.total_tile_pool_upload_cpu_ms += upload_cpu_ms;
            if (upload_cpu_ms > stats.max_tile_pool_upload_cpu_ms) {
                stats.max_tile_pool_upload_cpu_ms = upload_cpu_ms;
            }
            return result;
        };

        // Upload to tile pool
        int layer = upload_to_pool();

        // Pool full — evict LRU tile and retry
        if (layer < 0 && tile_pool_->GetFreeLayers() == 0) {
            const auto eviction_start = std::chrono::steady_clock::now();
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

            }
            record_stage(stats.total_eviction_cpu_ms,
                         stats.max_eviction_cpu_ms,
                         eviction_start);
            if (candidate.has_value()) {
                // Transfer time belongs to the physical upload metric, not
                // the LRU bookkeeping metric above.
                layer = upload_to_pool();
            }
        }

        if (layer >= 0) {
            // Update state to Loaded and decrement pending counter. If the
            // request was cancelled while its command was queued, do not leave
            // an unowned physical page behind.
            bool installed = false;
            {
                const auto state_start = std::chrono::steady_clock::now();
                std::unique_lock<std::shared_mutex> lock(state_mutex_);
                auto it = tile_states_.find(cmd->coords);
                if (it != tile_states_.end() && it->second.status == TileStatus::Loading) {
                    it->second.status = TileStatus::Loaded;
                    it->second.pool_layer = layer;
                    it->second.imagery_key = *cmd->imagery_key;
                    pending_load_count_.fetch_sub(1);
                    installed = true;
                }
                record_stage(stats.total_residency_state_cpu_ms,
                             stats.max_residency_state_cpu_ms,
                             state_start);
            }

            if (installed) {
                ++stats.commands_installed;
                spdlog::trace("Tile {} uploaded to pool layer {}",
                              cmd->coords.GetKey(), layer);
            } else {
                const auto eviction_start = std::chrono::steady_clock::now();
                tile_pool_->EvictTile(*cmd->imagery_key);
                record_stage(stats.total_eviction_cpu_ms,
                             stats.max_eviction_cpu_ms,
                             eviction_start);
                spdlog::debug("Discarded upload whose request state was removed: {}",
                              cmd->coords.GetKey());
            }
        } else {
            // Upload failed — remove from pending state
            const auto state_start = std::chrono::steady_clock::now();
            std::unique_lock<std::shared_mutex> lock(state_mutex_);
            auto it = tile_states_.find(cmd->coords);
            if (it != tile_states_.end() && it->second.status == TileStatus::Loading) {
                tile_states_.erase(it);
                pending_load_count_.fetch_sub(1);
            }
            record_stage(stats.total_residency_state_cpu_ms,
                         stats.max_residency_state_cpu_ms,
                         state_start);
            spdlog::warn("Failed to upload tile {} to pool", cmd->coords.GetKey());
        }

        if (cmd->on_complete) {
            cmd->on_complete(cmd->coords);
        }
        finish_command();
    }

    stats.queue_depth_after = upload_queue_->Size();
    return stats;
}

void TileTextureCoordinator::UpdateActiveRequests(
    const std::vector<TileCoordinates>& exact_tiles,
    const std::vector<TileCoordinates>& ancestor_tiles) {
    std::unordered_set<TileCoordinates, TileCoordinatesHash> active_tiles;
    active_tiles.reserve(exact_tiles.size() + ancestor_tiles.size());
    std::unordered_map<TileCoordinates, int, TileCoordinatesHash> upload_priorities;
    upload_priorities.reserve(exact_tiles.size() + ancestor_tiles.size());

    for (const TileCoordinates& tile : exact_tiles) {
        active_tiles.insert(tile);
        upload_priorities.insert_or_assign(tile, 0);
    }
    for (const TileCoordinates& tile : ancestor_tiles) {
        active_tiles.insert(tile);
        upload_priorities.try_emplace(tile, 1);
    }

    std::vector<std::unique_ptr<GLUploadCommand>> retired_commands =
        upload_queue_->SetActivePriorities(std::move(upload_priorities));
    std::vector<TileCoordinates> obsolete_requests;
    obsolete_requests.reserve(retired_commands.size());
    for (const auto& command : retired_commands) {
        if (command) {
            obsolete_requests.push_back(command->coords);
        }
    }
    worker_pool_->ReclaimUploadCommands(std::move(retired_commands));

    const std::vector<TileCoordinates> cancelled_requests =
        worker_pool_->CancelQueuedRequestsExcept(active_tiles);
    obsolete_requests.insert(
        obsolete_requests.end(), cancelled_requests.begin(), cancelled_requests.end());

    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);
        std::unordered_set<TileCoordinates, TileCoordinatesHash> unique_obsolete_requests(
            obsolete_requests.begin(), obsolete_requests.end());
        for (const TileCoordinates& tile : unique_obsolete_requests) {
            const auto state = tile_states_.find(tile);
            if (state != tile_states_.end() && state->second.status == TileStatus::Loading) {
                tile_states_.erase(state);
                pending_load_count_.fetch_sub(1);
            }
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

void TileTextureCoordinator::OnTileLoadDiscarded(const TileCoordinates& coords) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    const auto state = tile_states_.find(coords);
    if (state != tile_states_.end() && state->second.status == TileStatus::Loading) {
        tile_states_.erase(state);
        pending_load_count_.fetch_sub(1);
    }
}

} // namespace earth_map
