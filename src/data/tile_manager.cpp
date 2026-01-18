/**
 * @file tile_manager.cpp
 * @brief Tile management system implementation
 */

#include "earth_map/renderer/tile_texture_manager.h"
#include <earth_map/data/tile_manager.h>
#include <earth_map/math/tile_mathematics.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <spdlog/spdlog.h>

namespace earth_map {

// Factory function
std::unique_ptr<TileManager> CreateTileManager(const TileManagerConfig& config) {
    return std::make_unique<BasicTileManager>(config);
}

BasicTileManager::BasicTileManager(const TileManagerConfig& config) 
    : config_(config) {
}

bool BasicTileManager::Initialize(const TileManagerConfig& config) {
    config_ = config;
    tiles_.clear();
    visible_tiles_.clear();
    
    spdlog::info("Initializing tile manager with max tiles: {}, max per frame: {}",
                 config_.max_tiles_in_memory, config_.max_tiles_per_frame);
    
    return true;
}

bool BasicTileManager::Update(const glm::vec3& camera_position,
                             const glm::mat4& view_matrix,
                             const glm::mat4& projection_matrix,
                             const glm::vec2& viewport_size) {
    // Check if update is needed
    bool camera_changed = glm::length(camera_position - last_camera_position_) > 1.0f;
    bool viewport_changed = glm::length(viewport_size - last_viewport_size_) > 1.0f;
    
    if (!camera_changed && !viewport_changed && !needs_update_) {
        return true;  // No update needed
    }
    
    last_camera_position_ = camera_position;
    last_viewport_size_ = viewport_size;
    needs_update_ = false;
    
    // Update tile visibility
    UpdateVisibility(camera_position, view_matrix, projection_matrix, viewport_size);
    
    // Sort visible tiles by priority
    std::sort(visible_tiles_.begin(), visible_tiles_.end(),
              [this](const Tile* a, const Tile* b) {
                  return CalculateTilePriority(*a, last_camera_position_) >
                         CalculateTilePriority(*b, last_camera_position_);
              });
    
    // Load tiles up to limit
    std::size_t tiles_loaded = 0;
    for (const auto& tile : visible_tiles_) {
        if (!tile->loaded && tiles_loaded < config_.max_tiles_per_frame) {
            LoadTile(tile->coordinates);
            tiles_loaded++;
        }
        if (tiles_loaded >= config_.max_tiles_per_frame) {
            break;
        }
    }
    
    spdlog::debug("Visible tiles: {}", visible_tiles_.size());
    
    return true;
}

void BasicTileManager::UpdateVisibility(const glm::vec3& camera_position,
                                       const glm::mat4& view_matrix,
                                       const glm::mat4& projection_matrix,
                                       const glm::vec2& viewport_size) {
    // For now unused
    (void)view_matrix;
    (void)projection_matrix;
    (void)viewport_size;

    visible_tiles_.clear();
    
    for (auto& tile : tiles_) {
        // Simple distance-based visibility (placeholder for proper frustum culling)
        float distance = glm::length(camera_position - glm::vec3(0.0f)); // Simplified
        tile->camera_distance = distance;
        
        bool should_be_visible = distance < CalculateMaxVisibleDistance(tile->lod_level);
        tile->visible = should_be_visible;
        
        if (tile->visible) {
            visible_tiles_.push_back(tile.get());
        }
    }
}

float BasicTileManager::CalculateScreenSpaceError(const TileCoordinates& tile_coords,
                                                 const glm::vec2& viewport_size,
                                                 float camera_distance) const {
    (void)viewport_size;
    
    // Get ground resolution for this zoom level
    (void)TileMathematics::GetGroundResolution(tile_coords.zoom);
    
    // Calculate screen-space error (simplified)
    float tile_size_degrees = 360.0f / std::pow(2.0f, static_cast<float>(tile_coords.zoom));
    float ground_resolution = tile_size_degrees * 111320.0f; // Rough meters per degree
    
    float screen_error = ground_resolution / camera_distance * viewport_size.y;
    return screen_error;
}

float BasicTileManager::CalculateMaxVisibleDistance(std::uint8_t lod_level) const {
    // Simple LOD distance calculation (placeholder)
    float max_distance = 1000.0f * std::pow(2.0f, static_cast<float>(lod_level));
    return max_distance;
}

float BasicTileManager::CalculateTilePriority(const Tile& tile,
                                           const glm::vec3& camera_position) const {
    (void)camera_position;
    float distance_score = 1.0f / (1.0f + tile.camera_distance * 0.001f);
    float lod_score = static_cast<float>(tile.lod_level) / 18.0f;  // Normalize to 0-1
    float error_score = 1.0f / (1.0f + tile.screen_error);
    
    return distance_score * lod_score * error_score;
}

std::vector<const Tile*> BasicTileManager::GetVisibleTiles() const {
    return visible_tiles_;
}

const Tile* BasicTileManager::GetTile(const TileCoordinates& coordinates) const {
    for (const auto& tile : tiles_) {
        if (tile->coordinates.x == coordinates.x &&
            tile->coordinates.y == coordinates.y &&
            tile->coordinates.zoom == coordinates.zoom) {
            return tile.get();
        }
    }
    return nullptr;
}

uint32_t BasicTileManager::GetTileTexture(const TileCoordinates &coordinates) const
{
    // Delegate to tile texture manager if available
    // This assumes texture_manager_ is added as a member variable
    if (texture_manager_) {
        return texture_manager_->GetTexture(coordinates);
    }
    
    // Fallback: return 0 (no texture) if no texture manager
    spdlog::debug("GetTileTexture called for ({}, {}, {}), no texture manager available",
                  coordinates.x, coordinates.y, coordinates.zoom);
    return 0;
}

bool BasicTileManager::LoadTile(const TileCoordinates& coordinates) {
    // Check if tile is already loaded
    if (GetTile(coordinates) != nullptr) {
        return true;
    }
    
    // Evict tiles if needed
    if (tiles_.size() >= config_.max_tiles_in_memory) {
        EvictTiles();
    }
    
    // Create new tile
    auto tile = std::make_unique<Tile>(coordinates);
    tile->geographic_bounds = TileMathematics::GetTileBounds(coordinates);
    
    spdlog::debug("Loading tile {}/{}/{}", coordinates.x, coordinates.y, coordinates.zoom);
    
    // Mark as loaded (placeholder for actual loading)
    tile->loaded = true;
    
    tiles_.push_back(std::move(tile));
    needs_update_ = true;  // Trigger visibility update
    
    return true;
}

std::future<bool> BasicTileManager::LoadTileTextureAsync(
    const TileCoordinates& coordinates,
    TileTextureCallback callback) {
    // spdlog::info("TileManager: LoadTileTextureAsync called for {}/{}/{}", coordinates.x, coordinates.y, coordinates.zoom);
    // Delegate to texture manager if available
    if (texture_manager_) {
        // spdlog::info("TileManager: Delegating to texture manager");
        return texture_manager_->LoadTextureAsync(coordinates, callback);
    }

    spdlog::warn("TileManager: No texture manager available");
    // Return failed future if no texture manager
    auto promise = std::make_shared<std::promise<bool>>();
    promise->set_value(false);
    return promise->get_future();
}

bool BasicTileManager::UnloadTile(const TileCoordinates& coordinates) {
    for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
        if ((*it)->coordinates.x == coordinates.x &&
            (*it)->coordinates.y == coordinates.y &&
            (*it)->coordinates.zoom == coordinates.zoom) {
            
            spdlog::debug("Unloading tile {}/{}/{}", coordinates.x, coordinates.y, coordinates.zoom);
            tiles_.erase(it);
            needs_update_ = true;  // Trigger visibility update
            return true;
        }
    }
    return false;
}

std::vector<const Tile*> BasicTileManager::GetTilesInBounds(
    const BoundingBox2D& bounds) const {
    std::vector<const Tile*> tiles_in_bounds;
    
    for (const auto& tile : tiles_) {
        if (bounds.Intersects(tile->geographic_bounds)) {
            tiles_in_bounds.push_back(tile.get());
        }
    }
    
    return tiles_in_bounds;
}

std::vector<TileCoordinates> BasicTileManager::GetTilesInBounds(
    const BoundingBox2D& bounds, int32_t zoom_level) const {
    // Delegate to TileMathematics for coordinate calculation
    return TileMathematics::GetTilesInBounds(bounds, zoom_level);
}

std::vector<const Tile*> BasicTileManager::GetTilesAtLOD(std::uint8_t lod_level) const {
    std::vector<const Tile*> tiles_at_lod;
    
    for (const auto& tile : tiles_) {
        if (tile->lod_level == lod_level) {
            tiles_at_lod.push_back(tile.get());
        }
    }
    
    return tiles_at_lod;
}

std::uint8_t BasicTileManager::CalculateOptimalLOD(
    const BoundingBox2D& geographic_bounds,
    const glm::vec2& viewport_size,
    float camera_distance) const {
    
    if (!config_.enable_auto_lod) {
        return config_.default_lod_level;
    }
    
    // Calculate screen area that the bounds would cover
    float bounds_width = geographic_bounds.max.x - geographic_bounds.min.x;
    float bounds_height = geographic_bounds.max.y - geographic_bounds.min.y;
    
    // Simplified LOD calculation
    float screen_area = viewport_size.x * viewport_size.y;
    float geographic_area = bounds_width * bounds_height;
    float scale_factor = std::sqrt(screen_area / geographic_area) * camera_distance * 0.001f;
    
    // Find best LOD level
    for (std::uint8_t lod = config_.min_lod_level; lod <= config_.max_lod_level; ++lod) {
        (void)TileMathematics::GetGroundResolution(lod);
        
        if (scale_factor < std::pow(2.0f, static_cast<float>(lod))) {
            return lod;
        }
    }
    
    return config_.max_lod_level;
}

std::pair<std::size_t, std::size_t> BasicTileManager::GetStatistics() const {
    std::size_t loaded_count = 0;
    std::size_t visible_count = 0;
    
    for (const auto& tile : tiles_) {
        if (tile->loaded) {
            loaded_count++;
        }
        if (tile->visible) {
            visible_count++;
        }
    }
    
    return {loaded_count, visible_count};
}

void BasicTileManager::Clear() {
    tiles_.clear();
    visible_tiles_.clear();
    needs_update_ = true;
}

TileManagerConfig BasicTileManager::GetConfiguration() const {
    return config_;
}

bool BasicTileManager::SetConfiguration(const TileManagerConfig& config) {
    config_ = config;
    needs_update_ = true;
    return true;
}

void BasicTileManager::SetTextureManager(std::shared_ptr<TileTextureManager> texture_manager) {
    texture_manager_ = texture_manager;
}

bool BasicTileManager::InitializeWithTextureManager(std::shared_ptr<TileTextureManager> texture_manager) {
    texture_manager_ = texture_manager;
    
    // Initialize tile manager with default config
    TileManagerConfig config;
    return Initialize(config);
}

void BasicTileManager::EvictTiles() {
    if (tiles_.size() <= config_.max_tiles_in_memory) {
        return;
    }
    
    std::size_t tiles_to_remove = tiles_.size() - config_.max_tiles_in_memory;
    
    switch (config_.eviction_strategy) {
        case TileManagerConfig::EvictionStrategy::LRU: {
            // Simple LRU based on age (placeholder)
            std::sort(tiles_.begin(), tiles_.end(),
                      [](const auto& a, const auto& b) {
                          return a->age > b->age;
                      });
            break;
        }
        case TileManagerConfig::EvictionStrategy::PRIORITY: {
            std::sort(tiles_.begin(), tiles_.end(),
                      [this](const auto& a, const auto& b) {
                          return CalculateTilePriority(*a, last_camera_position_) <
                                 CalculateTilePriority(*b, last_camera_position_);
                      });
            break;
        }
        case TileManagerConfig::EvictionStrategy::DISTANCE: {
            std::sort(tiles_.begin(), tiles_.end(),
                      [](const auto& a, const auto& b) {
                          return a->camera_distance > b->camera_distance;
                      });
            break;
        }
    }
    
    // Remove tiles with lowest priority
    tiles_.erase(tiles_.begin(), tiles_.begin() + tiles_to_remove);
    needs_update_ = true;  // Trigger visibility update
}

std::vector<const Tile*> BasicTileManager::GetTilesByPriority() const {
    std::vector<const Tile*> sorted_tiles;
    
    for (const auto& tile : tiles_) {
        sorted_tiles.push_back(tile.get());
    }
    
    std::sort(sorted_tiles.begin(), sorted_tiles.end(),
              [this](const Tile* a, const Tile* b) {
                  return CalculateTilePriority(*a, last_camera_position_) >
                         CalculateTilePriority(*b, last_camera_position_);
              });
    
    return sorted_tiles;
}

Tile* BasicTileManager::FindOrCreateTile(const TileCoordinates& coordinates) {
    // Check if tile already exists
    if (auto* existing_tile = GetTile(coordinates)) {
        return const_cast<Tile*>(existing_tile);
    }
    
    // Create new tile
    auto tile = std::make_unique<Tile>(coordinates);
    tile->geographic_bounds = TileMathematics::GetTileBounds(coordinates);
    
    Tile* tile_ptr = tile.get();
    tiles_.push_back(std::move(tile));
    
    return tile_ptr;
}

} // namespace earth_map
