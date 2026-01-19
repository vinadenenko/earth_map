#pragma once

/**
 * @file tile_index.h
 * @brief Tile spatial indexing system with quadtree support
 * 
 * Provides efficient spatial indexing for tiles using quadtree data structure,
 * supporting fast spatial queries, visibility culling, and tile priority calculations.
 */

#include <earth_map/math/tile_mathematics.h>
#include <earth_map/math/bounding_box.h>
#include <glm/vec2.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <map>

namespace earth_map {

/**
 * @brief Quadtree node for tile indexing
 */
struct QuadtreeNode {
    /** Node bounds in geographic coordinates */
    BoundingBox2D bounds;
    
    /** Node level in quadtree hierarchy */
    std::uint8_t level = 0;
    
    /** Child nodes (4 children: NW, NE, SW, SE) */
    std::array<std::shared_ptr<QuadtreeNode>, 4> children;
    
    /** Tiles contained in this node */
    std::vector<TileCoordinates> tiles;
    
    /** Node center point */
    glm::dvec2 center;
    
    /** Node dimensions */
    glm::dvec2 size;
    
    /** Maximum tiles per node before subdivision */
    std::size_t max_tiles = 10;
    
    /** Maximum depth of quadtree */
    std::uint8_t max_depth = 20;
    
    /** Whether this node is a leaf */
    bool is_leaf = true;
    
    /**
     * @brief Constructor
     */
    QuadtreeNode(const BoundingBox2D& node_bounds, 
                 std::uint8_t node_level = 0,
                 std::size_t max_tiles_per_node = 10,
                 std::uint8_t max_tree_depth = 20);
    
    /**
     * @brief Insert tile into this node
     */
    bool Insert(const TileCoordinates& tile);
    
    /**
     * @brief Remove tile from this node
     */
    bool Remove(const TileCoordinates& tile);
    
    /**
     * @brief Query tiles within bounds
     */
    std::vector<TileCoordinates> Query(const BoundingBox2D& query_bounds) const;
    
    /**
     * @brief Get all tiles in this node and children
     */
    std::vector<TileCoordinates> GetAllTiles() const;
    
    /**
     * @brief Check if point is within this node
     */
    bool Contains(const glm::dvec2& point) const;
    
    /**
     * @brief Check if bounds intersect this node
     */
    bool Intersects(const BoundingBox2D& query_bounds) const;
    
    /**
     * @brief Subdivide node into 4 children
     */
    void Subdivide();
    
    /**
     * @brief Get child index for tile coordinates
     */
    int GetChildIndex(const TileCoordinates& tile) const;
    
    /**
     * @brief Get child bounds
     */
    BoundingBox2D GetChildBounds(int child_index) const;
    
    /**
     * @brief Clear all tiles and children
     */
    void Clear();
    
    /**
     * @brief Get node statistics
     */
    struct NodeStats {
        std::size_t tile_count = 0;
        std::size_t node_count = 0;
        std::size_t leaf_count = 0;
        std::size_t max_depth = 0;
    };
    
    NodeStats GetStatistics() const;
};

/**
 * @brief Tile index configuration
 */
struct TileIndexConfig {
    /** Maximum tiles per quadtree node */
    std::size_t max_tiles_per_node = 10;
    
    /** Maximum quadtree depth */
    std::uint8_t max_quadtree_depth = 20;
    
    /** Enable automatic rebuilding */
    bool enable_auto_rebuild = true;
    
    /** Rebuild threshold (percentage of tiles moved) */
    float rebuild_threshold = 0.2f;
    
    /** Enable spatial culling */
    bool enable_spatial_culling = true;
    
    /** Enable priority sorting */
    bool enable_priority_sorting = true;
    
    /** Cache size for query results */
    std::size_t query_cache_size = 1000;
    
    /** Update frequency (in frames) */
    std::uint32_t update_frequency = 1;
};

/**
 * @brief Tile index interface
 * 
 * Provides spatial indexing for tiles with quadtree data structure,
 * supporting fast spatial queries, visibility culling, and tile priority calculations.
 */
class TileIndex {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~TileIndex() = default;
    
    /**
     * @brief Initialize the tile index
     * 
     * @param config Configuration parameters
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize(const TileIndexConfig& config) = 0;
    
    /**
     * @brief Insert tile into index
     * 
     * @param tile Tile coordinates to insert
     * @return true if insertion succeeded, false otherwise
     */
    virtual bool Insert(const TileCoordinates& tile) = 0;
    
    /**
     * @brief Remove tile from index
     * 
     * @param tile Tile coordinates to remove
     * @return true if removal succeeded, false otherwise
     */
    virtual bool Remove(const TileCoordinates& tile) = 0;
    
    /**
     * @brief Update tile in index
     * 
     * @param old_tile Old tile coordinates
     * @param new_tile New tile coordinates
     * @return true if update succeeded, false otherwise
     */
    virtual bool Update(const TileCoordinates& old_tile, 
                       const TileCoordinates& new_tile) = 0;
    
    /**
     * @brief Query tiles within geographic bounds
     * 
     * @param bounds Geographic bounds to query
     * @return std::vector<TileCoordinates> List of tiles within bounds
     */
    virtual std::vector<TileCoordinates> Query(const BoundingBox2D& bounds) const = 0;
    
    /**
     * @brief Query tiles within geographic bounds at specific zoom
     * 
     * @param bounds Geographic bounds to query
     * @param zoom_level Zoom level to filter
     * @return std::vector<TileCoordinates> List of tiles within bounds at zoom
     */
    virtual std::vector<TileCoordinates> Query(const BoundingBox2D& bounds,
                                               std::int32_t zoom_level) const = 0;
    
    /**
     * @brief Query tiles within zoom range
     * 
     * @param bounds Geographic bounds to query
     * @param min_zoom Minimum zoom level (inclusive)
     * @param max_zoom Maximum zoom level (inclusive)
     * @return std::vector<TileCoordinates> List of tiles within bounds and zoom range
     */
    virtual std::vector<TileCoordinates> Query(const BoundingBox2D& bounds,
                                               std::int32_t min_zoom,
                                               std::int32_t max_zoom) const = 0;
    
    /**
     * @brief Query tiles visible from camera position
     * 
     * @param camera_position Camera position in world coordinates
     * @param view_matrix Current view matrix
     * @param projection_matrix Current projection matrix
     * @param viewport_size Current viewport size
     * @return std::vector<TileCoordinates> List of visible tiles
     */
    virtual std::vector<TileCoordinates> QueryVisible(
        const glm::vec3& camera_position,
        const glm::mat4& view_matrix,
        const glm::mat4& projection_matrix,
        const glm::vec2& viewport_size) const = 0;
    
    /**
     * @brief Get tiles at specific zoom level
     * 
     * @param zoom_level Zoom level to query
     * @return std::vector<TileCoordinates> List of tiles at zoom level
     */
    virtual std::vector<TileCoordinates> GetTilesAtZoom(std::int32_t zoom_level) const = 0;
    
    /**
     * @brief Get tiles within zoom range
     * 
     * @param min_zoom Minimum zoom level (inclusive)
     * @param max_zoom Maximum zoom level (inclusive)
     * @return std::vector<TileCoordinates> List of tiles within zoom range
     */
    virtual std::vector<TileCoordinates> GetTilesInRange(std::int32_t min_zoom,
                                                         std::int32_t max_zoom) const = 0;
    
    /**
     * @brief Get neighbor tiles
     * 
     * @param tile Center tile coordinates
     * @return std::array<TileCoordinates, 8> Array of 8 neighboring tiles
     */
    virtual std::array<TileCoordinates, 8> GetNeighbors(const TileCoordinates& tile) const = 0;
    
    /**
     * @brief Get parent tile
     * 
     * @param tile Child tile coordinates
     * @return std::optional<TileCoordinates> Parent tile or empty if at root
     */
    virtual std::optional<TileCoordinates> GetParent(const TileCoordinates& tile) const = 0;
    
    /**
     * @brief Get child tiles
     * 
     * @param tile Parent tile coordinates
     * @return std::array<TileCoordinates, 4> Array of 4 child tiles
     */
    virtual std::array<TileCoordinates, 4> GetChildren(const TileCoordinates& tile) const = 0;
    
    /**
     * @ Clear all tiles from index
     */
    virtual void Clear() = 0;
    
    /**
     * @brief Rebuild index from scratch
     */
    virtual void Rebuild() = 0;
    
    /**
     * @brief Get index statistics
     */
    struct IndexStats {
        std::size_t total_tiles = 0;
        std::size_t total_nodes = 0;
        std::size_t leaf_nodes = 0;
        std::size_t max_depth = 0;
        std::size_t query_count = 0;
        float average_query_time_ms = 0.0f;
        std::map<std::int32_t, std::size_t> tiles_per_zoom;
    };
    
    virtual IndexStats GetStatistics() const = 0;
    
    /**
     * @brief Get configuration
     * 
     * @return TileIndexConfig Current configuration
     */
    virtual TileIndexConfig GetConfiguration() const = 0;
    
    /**
     * @brief Set configuration
     * 
     * @param config New configuration
     * @return true if configuration was applied, false otherwise
     */
    virtual bool SetConfiguration(const TileIndexConfig& config) = 0;
    
    /**
     * @brief Check if tile exists in index
     * 
     * @param tile Tile coordinates to check
     * @return true if tile exists, false otherwise
     */
    virtual bool Contains(const TileCoordinates& tile) const = 0;
    
    /**
     * @brief Get total tile count
     * 
     * @return std::size_t Total number of tiles in index
     */
    virtual std::size_t GetTileCount() const = 0;
    
    /**
     * @brief Update index (called periodically)
     * 
     * Optimizes internal data structures and performs maintenance tasks.
     */
    virtual void Update() = 0;

protected:
    /**
     * @brief Protected constructor
     */
    TileIndex() = default;
};

/**
 * @brief Factory function to create tile index
 * 
 * @param config Configuration parameters
 * @return std::unique_ptr<TileIndex> New tile index instance
 */
std::unique_ptr<TileIndex> CreateTileIndex(const TileIndexConfig& config = {});

} // namespace earth_map
