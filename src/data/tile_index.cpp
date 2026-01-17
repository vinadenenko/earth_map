/**
 * @file tile_index.cpp
 * @brief Tile spatial indexing system implementation
 */

#include <earth_map/data/tile_index.h>
#include <earth_map/math/tile_mathematics.h>
#include <algorithm>
#include <stack>
#include <optional>
#include <chrono>
#include <spdlog/spdlog.h>

namespace earth_map {

// QuadtreeNode implementation
QuadtreeNode::QuadtreeNode(const BoundingBox2D& node_bounds,
                           std::uint8_t node_level,
                           std::size_t max_tiles_per_node,
                           std::uint8_t max_tree_depth)
    : bounds(node_bounds)
    , level(node_level)
    , max_tiles(max_tiles_per_node)
    , max_depth(max_tree_depth)
    , is_leaf(true) {
    
    center = glm::dvec2((bounds.min.x + bounds.max.x) / 2.0,
                        (bounds.min.y + bounds.max.y) / 2.0);
    size = glm::dvec2(bounds.max.x - bounds.min.x,
                      bounds.max.y - bounds.min.y);
    
    children.fill(nullptr);
}

bool QuadtreeNode::Insert(const TileCoordinates& tile) {
    // Check if tile bounds intersect this node
    BoundingBox2D tile_bounds = TileMathematics::GetTileBounds(tile);
    if (!Intersects(tile_bounds)) {
        return false;
    }
    
    // If leaf node and has space, insert here
    if (is_leaf && tiles.size() < max_tiles) {
        tiles.push_back(tile);
        return true;
    }
    
    // Subdivide if needed
    if (is_leaf) {
        Subdivide();
    }
    
    // Try to insert into children
    for (auto& child : children) {
        if (child && child->Insert(tile)) {
            return true;
        }
    }
    
    // If couldn't insert into children, keep in this node
    tiles.push_back(tile);
    return true;
}

bool QuadtreeNode::Remove(const TileCoordinates& tile) {
    // Check if tile is in this node
    auto it = std::find(tiles.begin(), tiles.end(), tile);
    if (it != tiles.end()) {
        tiles.erase(it);
        return true;
    }
    
    // Try to remove from children
    for (auto& child : children) {
        if (child && child->Remove(tile)) {
            return true;
        }
    }
    
    return false;
}

std::vector<TileCoordinates> QuadtreeNode::Query(const BoundingBox2D& query_bounds) const {
    std::vector<TileCoordinates> results;
    
    // Check if query bounds intersect this node
    if (!Intersects(query_bounds)) {
        return results;
    }
    
    // Add tiles in this node that intersect query bounds
    for (const auto& tile : tiles) {
        BoundingBox2D tile_bounds = TileMathematics::GetTileBounds(tile);
        if (query_bounds.Intersects(tile_bounds)) {
            results.push_back(tile);
        }
    }
    
    // Query children
    if (!is_leaf) {
        for (const auto& child : children) {
            if (child) {
                auto child_results = child->Query(query_bounds);
                results.insert(results.end(), child_results.begin(), child_results.end());
            }
        }
    }
    
    return results;
}

std::vector<TileCoordinates> QuadtreeNode::GetAllTiles() const {
    std::vector<TileCoordinates> all_tiles;
    
    // Add tiles from this node
    all_tiles.insert(all_tiles.end(), tiles.begin(), tiles.end());
    
    // Add tiles from children
    if (!is_leaf) {
        for (const auto& child : children) {
            if (child) {
                auto child_tiles = child->GetAllTiles();
                all_tiles.insert(all_tiles.end(), child_tiles.begin(), child_tiles.end());
            }
        }
    }
    
    return all_tiles;
}

bool QuadtreeNode::Contains(const glm::dvec2& point) const {
    return point.x >= bounds.min.x && point.x <= bounds.max.x &&
           point.y >= bounds.min.y && point.y <= bounds.max.y;
}

bool QuadtreeNode::Intersects(const BoundingBox2D& query_bounds) const {
    return bounds.Intersects(query_bounds);
}

void QuadtreeNode::Subdivide() {
    if (level >= max_depth) {
        return;  // Reached max depth
    }
    
    is_leaf = false;
    
    // Create 4 child nodes (NW, NE, SW, SE)
    for (int i = 0; i < 4; ++i) {
        BoundingBox2D child_bounds = GetChildBounds(i);
        children[i] = std::make_shared<QuadtreeNode>(
            child_bounds, level + 1, max_tiles, max_depth);
    }
    
    // Redistribute existing tiles to children
    std::vector<TileCoordinates> tiles_to_redistribute = tiles;
    tiles.clear();
    
    for (const auto& tile : tiles_to_redistribute) {
        for (auto& child : children) {
            if (child && child->Insert(tile)) {
                break;
            }
        }
    }
}

int QuadtreeNode::GetChildIndex(const TileCoordinates& tile) const {
    BoundingBox2D tile_bounds = TileMathematics::GetTileBounds(tile);
    glm::dvec2 tile_center = glm::dvec2((tile_bounds.min.x + tile_bounds.max.x) / 2.0,
                                         (tile_bounds.min.y + tile_bounds.max.y) / 2.0);
    
    int index = 0;
    if (tile_center.x > center.x) index += 1;  // East
    if (tile_center.y > center.y) index += 2;  // North
    
    return index;
}

BoundingBox2D QuadtreeNode::GetChildBounds(int child_index) const {
    BoundingBox2D child_bounds;
    
    switch (child_index) {
        case 0: // SW
            child_bounds.min = bounds.min;
            child_bounds.max = center;
            break;
        case 1: // SE
            child_bounds.min = glm::dvec2(center.x, bounds.min.y);
            child_bounds.max = glm::dvec2(bounds.max.x, center.y);
            break;
        case 2: // NW
            child_bounds.min = glm::dvec2(bounds.min.x, center.y);
            child_bounds.max = glm::dvec2(center.x, bounds.max.y);
            break;
        case 3: // NE
            child_bounds.min = center;
            child_bounds.max = bounds.max;
            break;
    }
    
    return child_bounds;
}

void QuadtreeNode::Clear() {
    tiles.clear();
    children.fill(nullptr);
    is_leaf = true;
}

QuadtreeNode::NodeStats QuadtreeNode::GetStatistics() const {
    NodeStats stats;
    
    stats.tile_count += tiles.size();
    stats.node_count++;
    
    if (is_leaf) {
        stats.leaf_count++;
    }
    
    stats.max_depth = level;
    
    if (!is_leaf) {
        for (const auto& child : children) {
            if (child) {
                auto child_stats = child->GetStatistics();
                stats.tile_count += child_stats.tile_count;
                stats.node_count += child_stats.node_count;
                stats.leaf_count += child_stats.leaf_count;
                stats.max_depth = std::max(stats.max_depth, child_stats.max_depth);
            }
        }
    }
    
    return stats;
}

/**
 * @brief Basic tile index implementation
 */
class BasicTileIndex : public TileIndex {
public:
    explicit BasicTileIndex(const TileIndexConfig& config) : config_(config) {}
    ~BasicTileIndex() override = default;
    
    bool Initialize(const TileIndexConfig& config) override;
    bool Insert(const TileCoordinates& tile) override;
    bool Remove(const TileCoordinates& tile) override;
    bool Update(const TileCoordinates& old_tile, const TileCoordinates& new_tile) override;
    
    std::vector<TileCoordinates> Query(const BoundingBox2D& bounds) const override;
    std::vector<TileCoordinates> Query(const BoundingBox2D& bounds,
                                       std::int32_t zoom_level) const override;
    std::vector<TileCoordinates> Query(const BoundingBox2D& bounds,
                                       std::int32_t min_zoom,
                                       std::int32_t max_zoom) const override;
    
    std::vector<TileCoordinates> QueryVisible(
        const glm::vec3& camera_position,
        const glm::mat4& view_matrix,
        const glm::mat4& projection_matrix,
        const glm::vec2& viewport_size) const override;
    
    std::vector<TileCoordinates> GetTilesAtZoom(std::int32_t zoom_level) const override;
    std::vector<TileCoordinates> GetTilesInRange(std::int32_t min_zoom,
                                                 std::int32_t max_zoom) const override;
    
    std::array<TileCoordinates, 8> GetNeighbors(const TileCoordinates& tile) const override;
    std::optional<TileCoordinates> GetParent(const TileCoordinates& tile) const override;
    std::array<TileCoordinates, 4> GetChildren(const TileCoordinates& tile) const override;
    
    void Clear() override;
    void Rebuild() override;
    
    IndexStats GetStatistics() const override;
    
    TileIndexConfig GetConfiguration() const override { return config_; }
    bool SetConfiguration(const TileIndexConfig& config) override;
    
    bool Contains(const TileCoordinates& tile) const override;
    std::size_t GetTileCount() const override;
    void Update() override;

private:
    TileIndexConfig config_;
    std::shared_ptr<QuadtreeNode> root_;
    std::unordered_map<TileCoordinates, BoundingBox2D, TileCoordinatesHash> tile_bounds_;
    
    // Statistics tracking
    mutable std::size_t query_count_ = 0;
    mutable std::uint64_t total_query_time_ms_ = 0;
    
    // Internal methods
    BoundingBox2D GetWorldBounds() const;
    void UpdateStatistics(const TileCoordinates& tile, bool added);
    std::vector<TileCoordinates> FilterByZoom(const std::vector<TileCoordinates>& tiles,
                                              std::int32_t zoom_level) const;
    std::vector<TileCoordinates> FilterByZoomRange(const std::vector<TileCoordinates>& tiles,
                                                   std::int32_t min_zoom,
                                                   std::int32_t max_zoom) const;
};

// Factory function
std::unique_ptr<TileIndex> CreateTileIndex(const TileIndexConfig& config) {
    return std::make_unique<BasicTileIndex>(config);
}

bool BasicTileIndex::Initialize(const TileIndexConfig& config) {
    config_ = config;
    
    // Create root node covering entire world
    BoundingBox2D world_bounds = GetWorldBounds();
    root_ = std::make_shared<QuadtreeNode>(
        world_bounds, 0, config_.max_tiles_per_node, config_.max_quadtree_depth);
    
    tile_bounds_.clear();
    
    spdlog::info("Tile index initialized with world bounds: [{}, {}] to [{}, {}]",
                 world_bounds.min.x, world_bounds.min.y,
                 world_bounds.max.x, world_bounds.max.y);
    
    return true;
}

bool BasicTileIndex::Insert(const TileCoordinates& tile) {
    if (!root_) {
        return false;
    }
    
    // Store tile bounds for faster access
    BoundingBox2D bounds = TileMathematics::GetTileBounds(tile);
    tile_bounds_[tile] = bounds;
    
    bool success = root_->Insert(tile);
    if (success) {
        UpdateStatistics(tile, true);
    }
    
    return success;
}

bool BasicTileIndex::Remove(const TileCoordinates& tile) {
    if (!root_) {
        return false;
    }
    
    bool success = root_->Remove(tile);
    if (success) {
        tile_bounds_.erase(tile);
        UpdateStatistics(tile, false);
    }
    
    return success;
}

bool BasicTileIndex::Update(const TileCoordinates& old_tile, const TileCoordinates& new_tile) {
    if (Remove(old_tile)) {
        return Insert(new_tile);
    }
    return false;
}

std::vector<TileCoordinates> BasicTileIndex::Query(const BoundingBox2D& bounds) const {
    auto start_time = std::chrono::steady_clock::now();
    
    std::vector<TileCoordinates> results;
    if (root_) {
        results = root_->Query(bounds);
    }
    
    // Update statistics
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    query_count_++;
    total_query_time_ms_ += duration.count();
    
    return results;
}

std::vector<TileCoordinates> BasicTileIndex::Query(const BoundingBox2D& bounds,
                                                   std::int32_t zoom_level) const {
    auto all_tiles = Query(bounds);
    return FilterByZoom(all_tiles, zoom_level);
}

std::vector<TileCoordinates> BasicTileIndex::Query(const BoundingBox2D& bounds,
                                                   std::int32_t min_zoom,
                                                   std::int32_t max_zoom) const {
    auto all_tiles = Query(bounds);
    return FilterByZoomRange(all_tiles, min_zoom, max_zoom);
}

std::vector<TileCoordinates> BasicTileIndex::QueryVisible(
    const glm::vec3& camera_position,
    const glm::mat4& view_matrix,
    const glm::mat4& projection_matrix,
    const glm::vec2& viewport_size) const {
    
    // Suppress unused parameter warnings for now
    (void)camera_position;
    (void)view_matrix;
    (void)projection_matrix;
    (void)viewport_size;
    
    // Simplified visibility query
    // In a full implementation, you would project the frustum and determine visible bounds
    
    // For now, just query a large area around the camera
    BoundingBox2D visible_bounds;
    visible_bounds.min = glm::dvec2(-180.0, -85.0);
    visible_bounds.max = glm::dvec2(180.0, 85.0);
    
    return Query(visible_bounds);
}

std::vector<TileCoordinates> BasicTileIndex::GetTilesAtZoom(std::int32_t zoom_level) const {
    BoundingBox2D world_bounds = GetWorldBounds();
    auto all_tiles = Query(world_bounds);
    return FilterByZoom(all_tiles, zoom_level);
}

std::vector<TileCoordinates> BasicTileIndex::GetTilesInRange(std::int32_t min_zoom,
                                                            std::int32_t max_zoom) const {
    BoundingBox2D world_bounds = GetWorldBounds();
    auto all_tiles = Query(world_bounds);
    return FilterByZoomRange(all_tiles, min_zoom, max_zoom);
}

std::array<TileCoordinates, 8> BasicTileIndex::GetNeighbors(const TileCoordinates& tile) const {
    std::array<TileCoordinates, 8> neighbors;
    
    // Neighbor offsets (N, NE, E, SE, S, SW, W, NW)
    std::array<std::pair<int, int>, 8> offsets = {{
        {0, 1},   // North
        {1, 1},   // Northeast
        {1, 0},   // East
        {1, -1},  // Southeast
        {0, -1},  // South
        {-1, -1}, // Southwest
        {-1, 0},  // West
        {-1, 1}   // Northwest
    }};
    
    for (int i = 0; i < 8; ++i) {
        neighbors[i] = TileCoordinates(
            tile.x + offsets[i].first,
            tile.y + offsets[i].second,
            tile.zoom
        );
    }
    
    return neighbors;
}

std::optional<TileCoordinates> BasicTileIndex::GetParent(const TileCoordinates& tile) const {
    if (tile.zoom == 0) {
        return std::nullopt;  // Root level has no parent
    }
    
    return TileCoordinates(
        tile.x / 2,
        tile.y / 2,
        tile.zoom - 1
    );
}

std::array<TileCoordinates, 4> BasicTileIndex::GetChildren(const TileCoordinates& tile) const {
    std::array<TileCoordinates, 4> children;
    
    for (int i = 0; i < 4; ++i) {
        int child_x = tile.x * 2 + (i % 2);
        int child_y = tile.y * 2 + (i / 2);
        children[i] = TileCoordinates(child_x, child_y, tile.zoom + 1);
    }
    
    return children;
}

void BasicTileIndex::Clear() {
    if (root_) {
        root_->Clear();
    }
    tile_bounds_.clear();
    
    query_count_ = 0;
    total_query_time_ms_ = 0;
    
    spdlog::info("Tile index cleared");
}

void BasicTileIndex::Rebuild() {
    // Store current tiles
    std::vector<TileCoordinates> current_tiles;
    if (root_) {
        current_tiles = root_->GetAllTiles();
    }
    
    // Reinitialize
    Initialize(config_);
    
    // Re-insert all tiles
    for (const auto& tile : current_tiles) {
        Insert(tile);
    }
    
    spdlog::info("Tile index rebuilt with {} tiles", current_tiles.size());
}

BasicTileIndex::IndexStats BasicTileIndex::GetStatistics() const {
    IndexStats stats;
    
    if (root_) {
        auto node_stats = root_->GetStatistics();
        stats.total_tiles = node_stats.tile_count;
        stats.total_nodes = node_stats.node_count;
        stats.leaf_nodes = node_stats.leaf_count;
        stats.max_depth = node_stats.max_depth;
    }
    
    stats.query_count = query_count_;
    
    if (query_count_ > 0) {
        stats.average_query_time_ms = static_cast<float>(total_query_time_ms_) / query_count_;
    }
    
    // Count tiles per zoom level
    for (const auto& [tile, bounds] : tile_bounds_) {
        stats.tiles_per_zoom[tile.zoom]++;
    }
    
    return stats;
}

bool BasicTileIndex::SetConfiguration(const TileIndexConfig& config) {
    config_ = config;
    
    // Rebuild if needed
    if (root_) {
        Rebuild();
    }
    
    return true;
}

bool BasicTileIndex::Contains(const TileCoordinates& tile) const {
    return tile_bounds_.find(tile) != tile_bounds_.end();
}

std::size_t BasicTileIndex::GetTileCount() const {
    return tile_bounds_.size();
}

void BasicTileIndex::Update() {
    // Periodic maintenance tasks
    if (config_.enable_auto_rebuild) {
        // Check if rebuild is needed based on some heuristic
        // For now, skip this optimization
    }
}

BoundingBox2D BasicTileIndex::GetWorldBounds() const {
    BoundingBox2D world_bounds;
    world_bounds.min = glm::dvec2(-180.0, -85.05112878);  // Web Mercator bounds
    world_bounds.max = glm::dvec2(180.0, 85.05112878);
    return world_bounds;
}

void BasicTileIndex::UpdateStatistics(const TileCoordinates& tile, bool added) {
    // Suppress unused parameter warnings for now
    (void)tile;
    (void)added;
    
    // Statistics are updated lazily in GetStatistics()
    // This could be optimized to maintain running counts
}

std::vector<TileCoordinates> BasicTileIndex::FilterByZoom(
    const std::vector<TileCoordinates>& tiles,
    std::int32_t zoom_level) const {
    
    std::vector<TileCoordinates> filtered;
    filtered.reserve(tiles.size());
    
    for (const auto& tile : tiles) {
        if (tile.zoom == zoom_level) {
            filtered.push_back(tile);
        }
    }
    
    return filtered;
}

std::vector<TileCoordinates> BasicTileIndex::FilterByZoomRange(
    const std::vector<TileCoordinates>& tiles,
    std::int32_t min_zoom,
    std::int32_t max_zoom) const {
    
    std::vector<TileCoordinates> filtered;
    filtered.reserve(tiles.size());
    
    for (const auto& tile : tiles) {
        if (tile.zoom >= min_zoom && tile.zoom <= max_zoom) {
            filtered.push_back(tile);
        }
    }
    
    return filtered;
}

} // namespace earth_map