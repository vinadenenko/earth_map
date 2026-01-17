/**
 * @file tile_manager.cpp
 * @brief Tile management system implementation
 */

#include <earth_map/data/tile_manager.h>
#include <algorithm>
#include <cmath>
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
    
    // Perform cache eviction if needed
    EvictTiles();
    
    return true;
}

void BasicTileManager::UpdateVisibility(const glm::vec3& camera_position,
                                     const glm::mat4& view_matrix,
                                     const glm::mat4& projection_matrix,
                                     const glm::vec2& viewport_size) {
    
    // Suppress unused parameter warnings for now
    (void)view_matrix;
    (void)projection_matrix;
    
    needs_update_ = false;
    visible_tiles_.clear();
    
    for (auto& tile : tiles_) {
        if (!tile) continue;
        
        // Calculate tile center in geographic coordinates
        glm::dvec2 tile_center_geo = tile->coordinates.GetCenter();
        
        // Calculate distance from camera to tile center
        float tile_distance = glm::distance(camera_position, 
                                          glm::vec3(tile_center_geo.x, tile_center_geo.y, 0.0f));
        
        // Calculate screen-space error for this tile
        tile->screen_error = CalculateScreenSpaceError(tile->coordinates,
                                                   viewport_size,
                                                   tile_distance);
        
        // Check if tile should be visible based on LOD
        float max_distance = CalculateMaxVisibleDistance(tile->lod_level);
            if (tile_distance > max_distance) {
                should_be_visible = false;
            }
        }
        
        tile->visible = should_be_visible;
        
        if (should_be_visible) {
            visible_tiles_.push_back(tile.get());
        }
    }
    
    // Sort visible tiles by priority
    std::sort(visible_tiles_.begin(), visible_tiles_.end(),
              [this](const Tile* a, const Tile* b) {
                  return CalculateTilePriority(*a, last_camera_position_) > 
                         CalculateTilePriority(*b, last_camera_position_);
              });
    
    // Limit number of visible tiles
    if (visible_tiles_.size() > config_.max_tiles_in_memory) {
        visible_tiles_.resize(config_.max_tiles_in_memory);
    }
    
    spdlog::debug("Visible tiles: {}", visible_tiles_.size());
}

float BasicTileManager::CalculateScreenSpaceError(const TileCoordinates& tile_coords,
                                             const glm::vec2& viewport_size,
                                             float camera_distance) const {
    // Calculate tile ground resolution at this LOD
    // Suppress unused variable warning for now
    (void)TileMathematics::GetGroundResolution(tile_coords.zoom);
    
    // Calculate projected tile size on screen
    float tile_size_degrees = 360.0f / std::pow(2.0f, static_cast<float>(tile_coords.zoom));
    
    // Simplified screen-space error calculation
    // TODO: Implement proper screen-space error with projection matrices
    float screen_size = (tile_size_degrees * viewport_size.y) / (2.0f * camera_distance);
    
    return screen_size;
}

float BasicTileManager::CalculateMaxVisibleDistance(std::uint8_t lod_level) const {
    // Simple distance calculation based on LOD level
    // Higher LOD levels (more detailed) visible from closer distances
    constexpr float base_distance = 10000.0f;  // 10km for base LOD
    constexpr float distance_multiplier = 0.5f;  // Each level halves the distance
    
    return base_distance * std::pow(distance_multiplier, static_cast<float>(lod_level));
}

float BasicTileManager::CalculateTilePriority(const Tile& tile,
                                         const glm::vec3& camera_position) const {
    // Suppress unused parameter warning for now
    (void)camera_position;
    // Priority based on distance, LOD level, and screen error
    float distance_score = 1.0f / (1.0f + tile.camera_distance * 0.001f);
    float lod_score = static_cast<float>(tile.lod_level) / 18.0f;  // Normalize to 0-1
    float error_score = 1.0f / (1.0f + tile.screen_error);
    
    // Combined priority score
    return distance_score * 0.5f + lod_score * 0.3f + error_score * 0.2f;
}

std::vector<const Tile*> BasicTileManager::GetVisibleTiles() const {
    return visible_tiles_;
}

const Tile* BasicTileManager::GetTile(const TileCoordinates& coordinates) const {
    for (const auto& tile : tiles_) {
        if (tile && tile->coordinates == coordinates) {
            return tile.get();
        }
    }
    return nullptr;
}

bool BasicTileManager::LoadTile(const TileCoordinates& coordinates) {
    // Check if tile already exists
    if (GetTile(coordinates) != nullptr) {
        return true;  // Already loaded
    }
    
    // Check memory limits
    if (tiles_.size() >= config_.max_tiles_in_memory) {
        spdlog::warn("Tile memory limit reached, cannot load tile {}/{}/{}", 
                    coordinates.x, coordinates.y, coordinates.zoom);
        return false;
    }
    
    // Create new tile
    auto tile = std::make_unique<Tile>(coordinates);
    tile->geographic_bounds = TileMathematics::GetTileBounds(coordinates);
    tile->loaded = true;
    tile->age = 0;
    
    spdlog::debug("Loading tile {}/{}/{}", coordinates.x, coordinates.y, coordinates.zoom);
    
    tiles_.push_back(std::move(tile));
    needs_update_ = true;  // Trigger visibility update
    
    return true;
}

bool BasicTileManager::UnloadTile(const TileCoordinates& coordinates) {
    for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
        if (*it && (*it)->coordinates == coordinates) {
            spdlog::debug("Unloading tile {}/{}/{}", 
                         coordinates.x, coordinates.y, coordinates.zoom);
            tiles_.erase(it);
            needs_update_ = true;  // Trigger visibility update
            return true;
        }
    }
    return false;  // Tile not found
}

std::vector<const Tile*> BasicTileManager::GetTilesInBounds(
    const BoundingBox2D& bounds) const {
    std::vector<const Tile*> tiles_in_bounds;
    
    for (const auto& tile : tiles_) {
        if (!tile) continue;
        
        if (bounds.Intersects(tile->geographic_bounds)) {
            tiles_in_bounds.push_back(tile.get());
        }
    }
    
    return tiles_in_bounds;
}

std::vector<const Tile*> BasicTileManager::GetTilesAtLOD(std::uint8_t lod_level) const {
    std::vector<const Tile*> tiles_at_lod;
    
    for (const auto& tile : tiles_) {
        if (!tile) continue;
        
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
    
    // Calculate required ground resolution
    float bounds_width = geographic_bounds.max.x - geographic_bounds.min.x;
    float bounds_height = geographic_bounds.max.y - geographic_bounds.min.y;
    float max_dimension = std::max(bounds_width, bounds_height);
    
    // Calculate screen coverage per degree at current distance
    float screen_coverage = viewport_size.y / (2.0f * camera_distance);
    float required_ground_resolution = max_dimension / screen_coverage;
    
    // Find LOD level that meets or exceeds required resolution
    for (std::uint8_t lod = config_.min_lod_level; lod <= config_.max_lod_level; ++lod) {
        // Suppress unused variable for now
        (void)TileMathematics::GetGroundResolution(lod);
        float lod_resolution = 1.0f;  // Default resolution
        
        if (lod_resolution <= required_ground_resolution * config_.lod_distance_bias) {
            return lod;
        }
    }
    
    // Return max LOD if none meet requirements
    return config_.max_lod_level;
}

std::pair<std::size_t, std::size_t> BasicTileManager::GetStatistics() const {
    std::size_t loaded_count = 0;
    std::size_t visible_count = 0;
    
    for (const auto& tile : tiles_) {
        if (!tile) continue;
        
        if (tile->loaded) {
            ++loaded_count;
        }
        if (tile->visible) {
            ++visible_count;
        }
    }
    
    return {loaded_count, visible_count};
}

void BasicTileManager::Clear() {
    tiles_.clear();
    visible_tiles_.clear();
    spdlog::info("Cleared all tiles from tile manager");
}

TileManagerConfig BasicTileManager::GetConfiguration() const {
    return config_;
}

bool BasicTileManager::SetConfiguration(const TileManagerConfig& config) {
    config_ = config;
    needs_update_ = true;
    
    spdlog::info("Updated tile manager configuration");
    return true;
}

void BasicTileManager::EvictTiles() {
    if (tiles_.size() <= config_.max_tiles_in_memory) {
        return;  // No eviction needed
    }
    
    std::size_t tiles_to_remove = tiles_.size() - config_.max_tiles_in_memory;
    
    switch (config_.eviction_strategy) {
        case TileManagerConfig::EvictionStrategy::LRU: {
            // Sort by age (oldest first)
            std::sort(tiles_.begin(), tiles_.end(),
                      [](const auto& a, const auto& b) {
                          return a->age > b->age;
                      });
            break;
        }
        
        case TileManagerConfig::EvictionStrategy::PRIORITY: {
            // Sort by priority (lowest first)
            std::sort(tiles_.begin(), tiles_.end(),
                      [this](const auto& a, const auto& b) {
                          return CalculateTilePriority(*a, last_camera_position_) < 
                                 CalculateTilePriority(*b, last_camera_position_);
                      });
            break;
        }
        
        case TileManagerConfig::EvictionStrategy::DISTANCE: {
            // Sort by distance (farthest first)
            std::sort(tiles_.begin(), tiles_.end(),
                      [](const auto& a, const auto& b) {
                          return a->camera_distance > b->camera_distance;
                      });
            break;
        }
    }
    
    // Remove least important tiles
    std::size_t removed_count = 0;
    for (auto it = tiles_.begin(); 
         it != tiles_.end() && removed_count < tiles_to_remove; ) {
        
        if (*it && !(*it)->visible) {  // Don't evict visible tiles
            spdlog::debug("Evicting tile {}/{}/{}", 
                         (*it)->coordinates.x, (*it)->coordinates.y, (*it)->coordinates.zoom);
            it = tiles_.erase(it);
            ++removed_count;
        } else {
            ++it;
        }
    }
    
    if (removed_count > 0) {
        needs_update_ = true;  // Trigger visibility update
        spdlog::debug("Evicted {} tiles", removed_count);
    }
}

std::vector<const Tile*> BasicTileManager::GetTilesByPriority() const {
    std::vector<const Tile*> sorted_tiles;
    
    for (const auto& tile : tiles_) {
        if (!tile) continue;
        sorted_tiles.push_back(tile.get());
    }
    
    // Sort by priority (highest first)
    std::sort(sorted_tiles.begin(), sorted_tiles.end(),
              [this](const Tile* a, const Tile* b) {
                  return CalculateTilePriority(*a, last_camera_position_) > 
                         CalculateTilePriority(*b, last_camera_position_);
              });
    
    return sorted_tiles;
}

Tile* BasicTileManager::FindOrCreateTile(const TileCoordinates& coordinates) {
    // Search for existing tile
    for (auto& tile : tiles_) {
        if (tile && tile->coordinates == coordinates) {
            return tile.get();
        }
    }
    
    // Create new tile if not found
    spdlog::debug("Creating new tile {}/{}/{}", 
                 coordinates.x, coordinates.y, coordinates.zoom);
    
    auto tile = std::make_unique<Tile>(coordinates);
    tile->geographic_bounds = TileMathematics::GetTileBounds(coordinates);
    
    Tile* tile_ptr = tile.get();
    tiles_.push_back(std::move(tile));
    
    return tile_ptr;
}

} // namespace earth_map