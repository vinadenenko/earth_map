#pragma once

/**
 * @file tile_mathematics.h
 * @brief Tile coordinate system and mathematics
 * 
 * Defines tile coordinate system utilities, quadtree indexing,
 * and tile-related mathematical operations for map tile management.
 */

#include "projection.h"
#include "bounding_box.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <string>

namespace earth_map {

// Import coordinate types from coordinates namespace
using coordinates::Geographic;

/**
 * @brief Tile coordinates (X, Y, Zoom)
 */
struct TileCoordinates {
    int32_t x;      ///< Tile X coordinate
    int32_t y;      ///< Tile Y coordinate
    int32_t zoom;   ///< Zoom level
    
    /**
     * @brief Default constructor
     */
    constexpr TileCoordinates() : x(0), y(0), zoom(0) {}
    
    /**
     * @brief Construct from coordinates and zoom
     * 
     * @param tile_x Tile X coordinate
     * @param tile_y Tile Y coordinate
     * @param tile_zoom Zoom level
     */
    constexpr TileCoordinates(int32_t tile_x, int32_t tile_y, int32_t tile_zoom)
        : x(tile_x), y(tile_y), zoom(tile_zoom) {}
    
    /**
     * @brief Check if tile coordinates are valid
     * 
     * @return true if valid, false otherwise
     */
    bool IsValid() const {
        if (zoom < 0 || zoom > 30) return false;
        const int32_t max_coord = 1 << zoom;
        return x >= 0 && x < max_coord && y >= 0 && y < max_coord;
    }
    
    /**
     * @brief Get parent tile coordinates
     * 
     * @return TileCoordinates Parent tile (one zoom level up)
     */
    TileCoordinates GetParent() const {
        return TileCoordinates(x >> 1, y >> 1, zoom - 1);
    }
    
    /**
     * @brief Get child tile coordinates
     * 
     * @return std::array<TileCoordinates, 4> Array of 4 child tiles (one zoom level down)
     */
    std::array<TileCoordinates, 4> GetChildren() const {
        const int32_t child_zoom = zoom + 1;
        return {
            TileCoordinates(x * 2, y * 2, child_zoom),
            TileCoordinates(x * 2 + 1, y * 2, child_zoom),
            TileCoordinates(x * 2, y * 2 + 1, child_zoom),
            TileCoordinates(x * 2 + 1, y * 2 + 1, child_zoom)
        };
    }
    
    /**
     * @brief Get tile key as string
     * 
     * @return std::string Tile key in format "zoom/x/y"
     */
    std::string GetKey() const {
        return std::to_string(zoom) + "/" + std::to_string(x) + "/" + std::to_string(y);
    }
    
    /**
     * @brief Calculate tile hash [UNUSED]
     * 
     * @return uint64_t Hash value for efficient storage
     */
    // uint64_t GetHash() const {
    //     return (static_cast<uint64_t>(zoom) << 42) |
    //            (static_cast<uint64_t>(x) << 21) |
    //            static_cast<uint64_t>(y);
    // }
    
    /**
     * @brief Get tile center in geographic coordinates
     * 
     * @return glm::dvec2 Geographic center point (lon, lat)
     */
    glm::dvec2 GetCenter() const {
        // Simple implementation - convert tile coordinates to geographic center
        double n = std::pow(2.0, zoom);
        double lon = (x + 0.5) / n * 360.0 - 180.0;
        double lat_rad = std::atan(std::sinh(M_PI * (1 - 2 * (y + 0.5) / n)));
        double lat = lat_rad * 180.0 / M_PI;
        return glm::dvec2(lon, lat);
    }
    
    /**
     * @brief Equality operator
     */
    bool operator==(const TileCoordinates& other) const {
        return x == other.x && y == other.y && zoom == other.zoom;
    }
    
    /**
     * @brief Inequality operator
     */
    bool operator!=(const TileCoordinates& other) const {
        return !(*this == other);
    }
    
    /**
     * @brief Less than operator (for sorting)
     */
    bool operator<(const TileCoordinates& other) const {
        if (zoom != other.zoom) return zoom < other.zoom;
        if (x != other.x) return x < other.x;
        return y < other.y;
    }
};

/**
 * @brief Hash function for TileCoordinates
 */
struct TileCoordinatesHash {
    // Fastest
    std::size_t operator()(const TileCoordinates& coords) const {
        std::hash<std::uint64_t> hasher;
        std::uint64_t combined = (static_cast<std::uint64_t>(coords.x) << 42) |
                                (static_cast<std::uint64_t>(coords.y) << 21) |
                                static_cast<std::uint64_t>(coords.zoom);
        return hasher(combined);
    }
    // std::size_t operator()(const TileCoordinates& c) const noexcept {
    //     std::uint64_t h = 14695981039346656037ULL; // FNV offset basis

    //     auto mix = [&h](std::uint64_t v) {
    //         h ^= v;
    //         h *= 1099511628211ULL; // FNV prime
    //     };

    //     mix(static_cast<std::uint64_t>(c.x));
    //     mix(static_cast<std::uint64_t>(c.y));
    //     mix(static_cast<std::uint64_t>(c.zoom));

    //     return static_cast<std::size_t>(h);
    // }
};

/**
 * @brief Quadtree tile key for hierarchical indexing
 */
struct QuadtreeKey {
    std::string key;  ///< Quadtree key string
    
    /**
     * @brief Default constructor
     */
    QuadtreeKey() = default;
    
    /**
     * @brief Construct from tile coordinates
     * 
     * @param tile Tile coordinates
     */
    explicit QuadtreeKey(const TileCoordinates& tile);
    
    /**
     * @brief Construct from key string
     * 
     * @param quadkey Quadtree key string
     */
    explicit QuadtreeKey(const std::string& quadkey);
    
    /**
     * @brief Convert to tile coordinates
     * 
     * @return TileCoordinates Tile coordinates
     */
    TileCoordinates ToTileCoordinates() const;
    
    /**
     * @brief Get parent quadtree key
     * 
     * @return QuadtreeKey Parent key
     */
    QuadtreeKey GetParent() const;
    
    /**
     * @brief Get child quadtree keys
     * 
     * @return std::array<QuadtreeKey, 4> Array of 4 child keys
     */
    std::array<QuadtreeKey, 4> GetChildren() const;
    
    /**
     * @brief Check if key is valid
     * 
     * @return true if valid, false otherwise
     */
    bool IsValid() const;
    
    /**
     * @brief Equality operator
     */
    bool operator==(const QuadtreeKey& other) const {
        return key == other.key;
    }
    
    /**
     * @brief Inequality operator
     */
    bool operator!=(const QuadtreeKey& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Tile mathematics and coordinate conversion utilities
 */
class TileMathematics {
public:
    /**
     * @brief Convert geographic coordinates to tile coordinates
     *
     * @param geo Geographic coordinates
     * @param zoom Zoom level
     * @return TileCoordinates Tile coordinates
     */
    static TileCoordinates GeographicToTile(const Geographic& geo, int32_t zoom);

    /**
     * @brief Convert tile coordinates to geographic coordinates (tile center)
     *
     * @param tile Tile coordinates
     * @return Geographic Geographic coordinates of tile center
     */
    static Geographic TileToGeographic(const TileCoordinates& tile);
    
    /**
     * @brief Get geographic bounds of a tile
     * 
     * @param tile Tile coordinates
     * @return BoundingBox2D Geographic bounds
     */
    static BoundingBox2D GetTileBounds(const TileCoordinates& tile);
    
    /**
     * @brief Get geographic bounds of a tile (with optional margins)
     * 
     * @param tile Tile coordinates
     * @param margin_x Horizontal margin in degrees (default: 0)
     * @param margin_y Vertical margin in degrees (default: 0)
     * @return BoundingBox2D Geographic bounds with margins
     */
    static BoundingBox2D GetTileBoundsWithMargins(const TileCoordinates& tile,
                                                  double margin_x = 0.0,
                                                  double margin_y = 0.0);
    
    /**
     * @brief Convert tile coordinates to normalized coordinates [0, 1]
     * 
     * @param tile Tile coordinates
     * @return glm::dvec2 Normalized coordinates (x, y)
     */
    static glm::dvec2 TileToNormalized(const TileCoordinates& tile);
    
    /**
     * @brief Convert normalized coordinates [0, 1] to tile coordinates
     * 
     * @param normalized Normalized coordinates
     * @param zoom Zoom level
     * @return TileCoordinates Tile coordinates
     */
    static TileCoordinates NormalizedToTile(const glm::dvec2& normalized, int32_t zoom);
    
    /**
     * @brief Get tiles that intersect a geographic bounding box
     * 
     * @param bounds Geographic bounding box
     * @param zoom Zoom level
     * @return std::vector<TileCoordinates> List of intersecting tiles
     */
    static std::vector<TileCoordinates> GetTilesInBounds(const BoundingBox2D& bounds, int32_t zoom);
    
    /**
     * @brief Get tiles that intersect a geographic bounding box (with margins)
     * 
     * @param bounds Geographic bounding box
     * @param zoom Zoom level
     * @param margin_tiles Margin in tiles
     * @return std::vector<TileCoordinates> List of intersecting tiles
     */
    static std::vector<TileCoordinates> GetTilesInBoundsWithMargins(const BoundingBox2D& bounds,
                                                                  int32_t zoom,
                                                                  int32_t margin_tiles = 1);
    
    /**
     * @brief Get tiles at multiple zoom levels that intersect bounds
     * 
     * @param bounds Geographic bounding box
     * @param min_zoom Minimum zoom level
     * @param max_zoom Maximum zoom level
     * @return std::vector<TileCoordinates> List of tiles at various zoom levels
     */
    static std::vector<TileCoordinates> GetTilesInBoundsMultipleZooms(const BoundingBox2D& bounds,
                                                                      int32_t min_zoom,
                                                                      int32_t max_zoom);
    
    /**
     * @brief Calculate distance between tiles in tiles
     * 
     * @param tile1 First tile
     * @param tile2 Second tile (must be same zoom level)
     * @return double Distance in tiles
     */
    static double TileDistance(const TileCoordinates& tile1, const TileCoordinates& tile2);
    
    /**
     * @brief Check if two tiles are adjacent
     * 
     * @param tile1 First tile
     * @param tile2 Second tile
     * @param diagonal Include diagonal adjacency (default: true)
     * @return bool True if tiles are adjacent, false otherwise
     */
    static bool AreTilesAdjacent(const TileCoordinates& tile1, 
                                 const TileCoordinates& tile2,
                                 bool diagonal = true);
    
    /**
     * @brief Get neighboring tiles
     * 
     * @param tile Center tile
     * @param diagonal Include diagonal neighbors (default: true)
     * @return std::vector<TileCoordinates> List of neighboring tiles
     */
    static std::vector<TileCoordinates> GetNeighborTiles(const TileCoordinates& tile,
                                                        bool diagonal = true);
    
    /**
     * @brief Calculate ground resolution at given zoom and latitude
     * 
     * @param zoom Zoom level
     * @param latitude Latitude in degrees
     * @return double Ground resolution in meters per pixel
     */
    static double CalculateGroundResolution(int32_t zoom, double latitude);
    
    /**
     * @brief Calculate map scale at given zoom and latitude
     * 
     * @param zoom Zoom level
     * @param latitude Latitude in degrees
     * @param screen_dpi Screen DPI (default: 96)
     * @return double Map scale (1:X)
     */
    static double CalculateMapScale(int32_t zoom, double latitude, double screen_dpi = 96.0);
    
    /**
     * @brief Get optimal zoom level for desired ground resolution
     * 
     * @param target_resolution Target resolution in meters per pixel
     * @param latitude Latitude in degrees
     * @return int32_t Optimal zoom level
     */
    static int32_t GetOptimalZoomLevel(double target_resolution, double latitude);
    
    /**
     * @brief Get tile URL for standard tile servers
     * 
     * @param tile Tile coordinates
     * @param url_template URL template with {x}, {y}, {z} placeholders
     * @return std::string Tile URL
     */
    static std::string GetTileURL(const TileCoordinates& tile, const std::string& url_template);
    
    /**
     * @brief Get subdomain for tile servers (load balancing)
     * 
     * @param tile Tile coordinates
     * @param subdomains Available subdomains (e.g., "abc")
     * @return char Selected subdomain
     */
    static char GetTileSubdomain(const TileCoordinates& tile, const std::string& subdomains = "abc");
    
    /**
     * @brief Get tile geographic center point
     * 
     * @param tile Tile coordinates
     * @return glm::dvec2 Geographic center (longitude, latitude)
     */
    static glm::dvec2 GetTileCenter(const TileCoordinates& tile);
    
    /**
     * @brief Get ground resolution at zoom level
     * 
     * @param zoom Zoom level
     * @return double Ground resolution in meters per pixel
     */
    static double GetGroundResolution(int32_t zoom);
};

/**
 * @brief Tile pyramid and LOD utilities
 */
class TilePyramid {
public:
    /**
     * @brief Get tiles in view frustum
     * 
     * @param bounds Geographic bounds
     * @param view_distance Maximum view distance in meters
     * @param min_zoom Minimum zoom level
     * @param max_zoom Maximum zoom level
     * @return std::vector<TileCoordinates> List of visible tiles
     */
    static std::vector<TileCoordinates> GetVisibleTiles(const BoundingBox2D& bounds,
                                                       double view_distance,
                                                       int32_t min_zoom,
                                                       int32_t max_zoom);
    
    /**
     * @brief Select optimal tiles for rendering (LOD selection)
     * 
     * @param bounds Geographic bounds
     * @param camera_distance Camera distance in meters
     * @param screen_size Screen size in pixels
     * @return std::vector<TileCoordinates> Optimal tile selection
     */
    static std::vector<TileCoordinates> SelectOptimalTiles(const BoundingBox2D& bounds,
                                                           double camera_distance,
                                                           const glm::ivec2& screen_size);
    
    /**
     * @brief Simplify tile selection for performance
     * 
     * @param tiles Original tile selection
     * @param max_tiles Maximum number of tiles
     * @return std::vector<TileCoordinates> Simplified selection
     */
    static std::vector<TileCoordinates> SimplifySelection(const std::vector<TileCoordinates>& tiles,
                                                          size_t max_tiles);
    
    /**
     * @brief Get tile hierarchy for a region
     * 
     * @param bounds Geographic bounds
     * @param max_zoom Maximum zoom level
     * @return std::vector<std::vector<TileCoordinates>> Tile hierarchy by zoom level
     */
    static std::vector<std::vector<TileCoordinates>> GetTileHierarchy(const BoundingBox2D& bounds,
                                                                     int32_t max_zoom);
};

/**
 * @brief Tile coordinate validation and utilities
 */
class TileValidator {
public:
    /**
     * @brief Validate tile coordinates
     * 
     * @param tile Tile coordinates
     * @return bool True if valid, false otherwise
     */
    static bool IsValidTile(const TileCoordinates& tile);
    
    /**
     * @brief Validate quadtree key
     * 
     * @param key Quadtree key
     * @return bool True if valid, false otherwise
     */
    static bool IsValidQuadTreeKey(const QuadtreeKey& key);
    
    /**
     * @brief Check if zoom level is supported
     * 
     * @param zoom Zoom level
     * @return bool True if supported, false otherwise
     */
    static bool IsSupportedZoom(int32_t zoom);
    
    /**
     * @brief Get valid zoom range
     * 
     * @return std::pair<int32_t, int32_t> Min and max zoom levels
     */
    static std::pair<int32_t, int32_t> GetZoomRange();
    
    /**
     * @brief Clamp zoom to valid range
     * 
     * @param zoom Input zoom level
     * @return int32_t Clamped zoom level
     */
    static int32_t ClampZoom(int32_t zoom);
    
    /**
     * @brief Check if tile coordinates wrap around (edge case)
     * 
     * @param tile Tile coordinates
     * @return bool True if tile wraps around, false otherwise
     */
    static bool IsWrapAroundTile(const TileCoordinates& tile);
};

} // namespace earth_map
