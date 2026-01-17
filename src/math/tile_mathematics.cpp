/**
 * @file tile_mathematics.cpp
 * @brief Tile coordinate system and mathematics implementation
 */

#include "../../include/earth_map/math/tile_mathematics.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace earth_map {

// QuadtreeKey implementation

QuadtreeKey::QuadtreeKey(const TileCoordinates& tile) {
    if (!TileValidator::IsValidTile(tile)) {
        key = "";
        return;
    }
    
    key = "";
    int32_t x = tile.x;
    int32_t y = tile.y;
    int32_t zoom = tile.zoom;
    
    for (int32_t i = zoom - 1; i >= 0; --i) {
        int32_t digit = 0;
        const int32_t mask = 1 << i;
        
        if ((x & mask) != 0) digit |= 1;
        if ((y & mask) != 0) digit |= 2;
        
        key += std::to_string(digit);
    }
}

QuadtreeKey::QuadtreeKey(const std::string& quadkey) : key(quadkey) {
    // Validation will be done in IsValid()
}

TileCoordinates QuadtreeKey::ToTileCoordinates() const {
    if (!IsValid()) {
        return TileCoordinates();
    }
    
    int32_t x = 0;
    int32_t y = 0;
    const int32_t zoom = static_cast<int32_t>(key.length());
    
    for (int32_t i = 0; i < zoom; ++i) {
        const int digit = key[i] - '0';
        if (digit < 0 || digit > 3) {
            return TileCoordinates();  // Invalid quadtree key
        }
        
        x <<= 1;
        y <<= 1;
        
        if ((digit & 1) != 0) x |= 1;
        if ((digit & 2) != 0) y |= 1;
    }
    
    return TileCoordinates(x, y, zoom);
}

QuadtreeKey QuadtreeKey::GetParent() const {
    if (key.empty()) return QuadtreeKey();
    return QuadtreeKey(key.substr(0, key.length() - 1));
}

std::array<QuadtreeKey, 4> QuadtreeKey::GetChildren() const {
    const std::string base_key = key;
    return {
        QuadtreeKey(base_key + "0"),
        QuadtreeKey(base_key + "1"),
        QuadtreeKey(base_key + "2"),
        QuadtreeKey(base_key + "3")
    };
}

bool QuadtreeKey::IsValid() const {
    if (key.empty()) return false;
    
    for (char c : key) {
        if (c < '0' || c > '3') return false;
    }
    
    const int32_t zoom = static_cast<int32_t>(key.length());
    return TileValidator::IsSupportedZoom(zoom);
}

// TileMathematics implementation

TileCoordinates TileMathematics::GeographicToTile(const GeographicCoordinates& geo, int32_t zoom) {
    if (!TileValidator::IsSupportedZoom(zoom)) {
        throw std::invalid_argument("Unsupported zoom level");
    }
    
    const auto web_mercator = std::static_pointer_cast<WebMercatorProjection>(
        ProjectionRegistry::GetProjection(ProjectionType::WEB_MERCATOR)
    );
    
    const ProjectedCoordinates proj = web_mercator->Project(geo);
    const double normalized_x = (proj.x + WebMercatorProjection::WEB_MERCATOR_HALF_WORLD) / 
                               WebMercatorProjection::WEB_MERCATOR_ORIGIN_SHIFT;
    const double normalized_y = (proj.y + WebMercatorProjection::WEB_MERCATOR_HALF_WORLD) / 
                               WebMercatorProjection::WEB_MERCATOR_ORIGIN_SHIFT;
    
    const int32_t n = 1 << zoom;
    // Use proper rounding instead of floor for better accuracy
    const int32_t x = static_cast<int32_t>(std::round(normalized_x * n - 0.5));
    const int32_t y = static_cast<int32_t>(std::round(normalized_y * n - 0.5));
    
    return TileCoordinates(std::max(0, std::min(x, n - 1)),
                          std::max(0, std::min(y, n - 1)),
                          zoom);
}

GeographicCoordinates TileMathematics::TileToGeographic(const TileCoordinates& tile) {
    if (!TileValidator::IsValidTile(tile)) {
        throw std::invalid_argument("Invalid tile coordinates");
    }
    
    const auto web_mercator = std::static_pointer_cast<WebMercatorProjection>(
        ProjectionRegistry::GetProjection(ProjectionType::WEB_MERCATOR)
    );
    
    const glm::dvec2 normalized = TileToNormalized(tile);
    // Use tile center for better round-trip precision
    const double tile_size = 1.0 / (1 << tile.zoom);
    const glm::dvec2 normalized_center(
        normalized.x + tile_size * 0.5,
        normalized.y + tile_size * 0.5
    );
    
    const glm::dvec2 proj_coords(
        normalized_center.x * WebMercatorProjection::WEB_MERCATOR_ORIGIN_SHIFT - WebMercatorProjection::WEB_MERCATOR_HALF_WORLD,
        normalized_center.y * WebMercatorProjection::WEB_MERCATOR_ORIGIN_SHIFT - WebMercatorProjection::WEB_MERCATOR_HALF_WORLD
    );
    
    return web_mercator->Unproject(ProjectedCoordinates(proj_coords.x, proj_coords.y));
}

BoundingBox2D TileMathematics::GetTileBounds(const TileCoordinates& tile) {
    if (!TileValidator::IsValidTile(tile)) {
        return BoundingBox2D();
    }
    
    const auto web_mercator = std::static_pointer_cast<WebMercatorProjection>(
        ProjectionRegistry::GetProjection(ProjectionType::WEB_MERCATOR)
    );
    
    const glm::dvec2 normalized = TileToNormalized(tile);
    const double tile_size = 1.0 / (1 << tile.zoom);
    
    const ProjectedCoordinates proj_min(
        normalized.x * WebMercatorProjection::WEB_MERCATOR_ORIGIN_SHIFT - WebMercatorProjection::WEB_MERCATOR_HALF_WORLD,
        normalized.y * WebMercatorProjection::WEB_MERCATOR_ORIGIN_SHIFT - WebMercatorProjection::WEB_MERCATOR_HALF_WORLD
    );
    
    const ProjectedCoordinates proj_max(
        (normalized.x + tile_size) * WebMercatorProjection::WEB_MERCATOR_ORIGIN_SHIFT - WebMercatorProjection::WEB_MERCATOR_HALF_WORLD,
        (normalized.y + tile_size) * WebMercatorProjection::WEB_MERCATOR_ORIGIN_SHIFT - WebMercatorProjection::WEB_MERCATOR_HALF_WORLD
    );
    
    const GeographicCoordinates geo_min = web_mercator->Unproject(proj_min);
    const GeographicCoordinates geo_max = web_mercator->Unproject(proj_max);
    
    return BoundingBox2D(
        glm::dvec2(geo_min.longitude, geo_min.latitude),
        glm::dvec2(geo_max.longitude, geo_max.latitude)
    );
}

BoundingBox2D TileMathematics::GetTileBoundsWithMargins(const TileCoordinates& tile,
                                                        double margin_x,
                                                        double margin_y) {
    BoundingBox2D bounds = GetTileBounds(tile);
    if (!bounds.IsValid()) return bounds;
    
    bounds.min.x -= margin_x;
    bounds.max.x += margin_x;
    bounds.min.y -= margin_y;
    bounds.max.y += margin_y;
    
    return bounds;
}

glm::dvec2 TileMathematics::TileToNormalized(const TileCoordinates& tile) {
    if (!TileValidator::IsValidTile(tile)) {
        return glm::dvec2(0.0, 0.0);
    }
    
    const int32_t n = 1 << tile.zoom;
    return glm::dvec2(
        static_cast<double>(tile.x) / n,
        static_cast<double>(tile.y) / n
    );
}

TileCoordinates TileMathematics::NormalizedToTile(const glm::dvec2& normalized, int32_t zoom) {
    if (!TileValidator::IsSupportedZoom(zoom)) {
        throw std::invalid_argument("Unsupported zoom level");
    }
    
    const int32_t n = 1 << zoom;
    const int32_t x = static_cast<int32_t>(std::floor(normalized.x * n));
    const int32_t y = static_cast<int32_t>(std::floor(normalized.y * n));
    
    return TileCoordinates(std::max(0, std::min(x, n - 1)),
                          std::max(0, std::min(y, n - 1)),
                          zoom);
}

std::vector<TileCoordinates> TileMathematics::GetTilesInBounds(const BoundingBox2D& bounds, int32_t zoom) {
    std::vector<TileCoordinates> tiles;
    
    if (!bounds.IsValid() || !TileValidator::IsSupportedZoom(zoom)) {
        return tiles;
    }
    
    // Convert bounds corners to tile coordinates
    const TileCoordinates min_tile = GeographicToTile(
        GeographicCoordinates(bounds.min.y, bounds.min.x, 0.0), zoom
    );
    const TileCoordinates max_tile = GeographicToTile(
        GeographicCoordinates(bounds.max.y, bounds.max.x, 0.0), zoom
    );
    
    // Clamp to valid range
    const int32_t n = 1 << zoom;
    const int32_t min_x = std::max(0, std::min(min_tile.x, max_tile.x));
    const int32_t max_x = std::min(n - 1, std::max(min_tile.x, max_tile.x));
    const int32_t min_y = std::max(0, std::min(min_tile.y, max_tile.y));
    const int32_t max_y = std::min(n - 1, std::max(min_tile.y, max_tile.y));
    
    tiles.reserve((max_x - min_x + 1) * (max_y - min_y + 1));
    
    for (int32_t x = min_x; x <= max_x; ++x) {
        for (int32_t y = min_y; y <= max_y; ++y) {
            tiles.emplace_back(x, y, zoom);
        }
    }
    
    return tiles;
}

std::vector<TileCoordinates> TileMathematics::GetTilesInBoundsWithMargins(const BoundingBox2D& bounds,
                                                                         int32_t zoom,
                                                                         int32_t margin_tiles) {
    // Simplified bounds expansion - just add margin to the bounds directly
    BoundingBox2D expanded_bounds = bounds;
    const double margin_degrees = margin_tiles * 0.1;  // Approximate margin in degrees
    expanded_bounds.min.x -= margin_degrees;
    expanded_bounds.max.x += margin_degrees;
    expanded_bounds.min.y -= margin_degrees;
    expanded_bounds.max.y += margin_degrees;
    
    return GetTilesInBounds(expanded_bounds, zoom);
}

std::vector<TileCoordinates> TileMathematics::GetTilesInBoundsMultipleZooms(const BoundingBox2D& bounds,
                                                                            int32_t min_zoom,
                                                                            int32_t max_zoom) {
    std::vector<TileCoordinates> tiles;
    
    min_zoom = TileValidator::ClampZoom(min_zoom);
    max_zoom = TileValidator::ClampZoom(max_zoom);
    
    for (int32_t zoom = min_zoom; zoom <= max_zoom; ++zoom) {
        const std::vector<TileCoordinates> zoom_tiles = GetTilesInBounds(bounds, zoom);
        tiles.insert(tiles.end(), zoom_tiles.begin(), zoom_tiles.end());
    }
    
    return tiles;
}

double TileMathematics::TileDistance(const TileCoordinates& tile1, const TileCoordinates& tile2) {
    if (tile1.zoom != tile2.zoom) {
        throw std::invalid_argument("Tiles must be at same zoom level");
    }
    
    const int32_t dx = tile2.x - tile1.x;
    const int32_t dy = tile2.y - tile1.y;
    return std::sqrt(static_cast<double>(dx * dx + dy * dy));
}

bool TileMathematics::AreTilesAdjacent(const TileCoordinates& tile1, 
                                     const TileCoordinates& tile2,
                                     bool diagonal) {
    if (tile1.zoom != tile2.zoom) return false;
    
    const int32_t dx = std::abs(tile2.x - tile1.x);
    const int32_t dy = std::abs(tile2.y - tile1.y);
    
    if (diagonal) {
        return (dx <= 1 && dy <= 1) && !(dx == 0 && dy == 0);
    } else {
        return (dx == 1 && dy == 0) || (dx == 0 && dy == 1);
    }
}

std::vector<TileCoordinates> TileMathematics::GetNeighborTiles(const TileCoordinates& tile,
                                                               bool diagonal) {
    std::vector<TileCoordinates> neighbors;
    
    if (!TileValidator::IsValidTile(tile)) {
        return neighbors;
    }
    
    const int32_t n = 1 << tile.zoom;
    
    if (diagonal) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            for (int32_t dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0) continue;
                
                const int32_t nx = tile.x + dx;
                const int32_t ny = tile.y + dy;
                
                if (nx >= 0 && nx < n && ny >= 0 && ny < n) {
                    neighbors.emplace_back(nx, ny, tile.zoom);
                }
            }
        }
    } else {
        // Cardinal directions only
        const int32_t directions[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        
        for (const auto& dir : directions) {
            const int32_t nx = tile.x + dir[0];
            const int32_t ny = tile.y + dir[1];
            
            if (nx >= 0 && nx < n && ny >= 0 && ny < n) {
                neighbors.emplace_back(nx, ny, tile.zoom);
            }
        }
    }
    
    return neighbors;
}

double TileMathematics::CalculateGroundResolution(int32_t zoom, double latitude) {
    if (!TileValidator::IsSupportedZoom(zoom)) {
        return 0.0;
    }
    
    const double lat_rad = CoordinateSystem::DegreesToRadians(latitude);
    const double cos_lat = std::cos(lat_rad);
    
    // Web Mercator ground resolution: 2π * R * cos(lat) / (256 * 2^zoom)
    return (2.0 * M_PI * WGS84Ellipsoid::SEMI_MAJOR_AXIS * cos_lat) / 
           (256.0 * (1 << zoom));
}

double TileMathematics::CalculateMapScale(int32_t zoom, double latitude, double screen_dpi) {
    const double ground_resolution = CalculateGroundResolution(zoom, latitude);
    const double meters_per_inch = 0.0254;
    const double screen_meters_per_pixel = meters_per_inch / screen_dpi;
    
    return ground_resolution / screen_meters_per_pixel;
}

int32_t TileMathematics::GetOptimalZoomLevel(double target_resolution, double latitude) {
    for (int32_t zoom = 0; zoom <= 30; ++zoom) {
        const double resolution = CalculateGroundResolution(zoom, latitude);
        if (resolution <= target_resolution) {
            return zoom;
        }
    }
    return 30;  // Maximum zoom
}

std::string TileMathematics::GetTileURL(const TileCoordinates& tile, const std::string& url_template) {
    std::string url = url_template;
    
    // Replace placeholders
    size_t pos = url.find("{x}");
    if (pos != std::string::npos) {
        url.replace(pos, 3, std::to_string(tile.x));
    }
    
    pos = url.find("{y}");
    if (pos != std::string::npos) {
        url.replace(pos, 3, std::to_string(tile.y));
    }
    
    pos = url.find("{z}");
    if (pos != std::string::npos) {
        url.replace(pos, 3, std::to_string(tile.zoom));
    }
    
    return url;
}

char TileMathematics::GetTileSubdomain(const TileCoordinates& tile, const std::string& subdomains) {
    if (subdomains.empty()) return '\0';
    
    const int32_t index = (tile.x + tile.y) % subdomains.length();
    return subdomains[index];
}

double TileMathematics::GetGroundResolution(int32_t zoom) {
    if (!TileValidator::IsSupportedZoom(zoom)) {
        return 0.0;
    }
    
    // Return ground resolution at equator for simplicity
    // This is the resolution when latitude = 0 (cos(lat) = 1)
    return (2.0 * M_PI * WGS84Ellipsoid::SEMI_MAJOR_AXIS) / 
           (256.0 * (1 << zoom));
}

// TilePyramid implementation (simplified versions)

std::vector<TileCoordinates> TilePyramid::GetVisibleTiles(const BoundingBox2D& bounds,
                                                          double /*view_distance*/,
                                                          int32_t min_zoom,
                                                          int32_t max_zoom) {
    // Simplified implementation - return tiles at all zoom levels in bounds
    return TileMathematics::GetTilesInBoundsMultipleZooms(bounds, min_zoom, max_zoom);
}

std::vector<TileCoordinates> TilePyramid::SelectOptimalTiles(const BoundingBox2D& bounds,
                                                             double camera_distance,
                                                             const glm::ivec2& /*screen_size*/) {
    // Simplified LOD selection
    int32_t optimal_zoom = 10;  // Default zoom
    
    // Select zoom based on camera distance
    if (camera_distance < 1000000) {
        optimal_zoom = 15;
    } else if (camera_distance < 5000000) {
        optimal_zoom = 12;
    } else if (camera_distance < 10000000) {
        optimal_zoom = 8;
    }
    
    return TileMathematics::GetTilesInBounds(bounds, optimal_zoom);
}

std::vector<TileCoordinates> TilePyramid::SimplifySelection(const std::vector<TileCoordinates>& tiles,
                                                          size_t max_tiles) {
    if (tiles.size() <= max_tiles) {
        return tiles;
    }
    
    // Simplified sampling - take every nth tile
    const size_t step = tiles.size() / max_tiles;
    std::vector<TileCoordinates> simplified;
    
    for (size_t i = 0; i < tiles.size(); i += step) {
        simplified.push_back(tiles[i]);
        if (simplified.size() >= max_tiles) break;
    }
    
    return simplified;
}

std::vector<std::vector<TileCoordinates>> TilePyramid::GetTileHierarchy(const BoundingBox2D& bounds,
                                                                        int32_t max_zoom) {
    std::vector<std::vector<TileCoordinates>> hierarchy;
    
    for (int32_t zoom = 0; zoom <= max_zoom; ++zoom) {
        const std::vector<TileCoordinates> zoom_tiles = TileMathematics::GetTilesInBounds(bounds, zoom);
        hierarchy.push_back(zoom_tiles);
    }
    
    return hierarchy;
}

// TileValidator implementation

bool TileValidator::IsValidTile(const TileCoordinates& tile) {
    return tile.IsValid();
}

bool TileValidator::IsValidQuadTreeKey(const QuadtreeKey& key) {
    return key.IsValid();
}

bool TileValidator::IsSupportedZoom(int32_t zoom) {
    return zoom >= 0 && zoom <= 30;
}

std::pair<int32_t, int32_t> TileValidator::GetZoomRange() {
    return std::make_pair(0, 30);
}

int32_t TileValidator::ClampZoom(int32_t zoom) {
    return std::max(0, std::min(zoom, 30));
}

bool TileValidator::IsWrapAroundTile(const TileCoordinates& tile) {
    // Tiles at the international date line wrap around
    const int32_t max_coord = 1 << tile.zoom;
    return tile.x == 0 || tile.x == max_coord - 1;
}

} // namespace earth_map
