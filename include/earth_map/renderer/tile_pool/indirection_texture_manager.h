#pragma once

/**
 * @file indirection_texture_manager.h
 * @brief Per-zoom indirection textures mapping tile (x,y) to pool layer index
 *
 * Manages indirection textures that map tile coordinates to layer indices in
 * the TileTexturePool. Two modes:
 *
 * - Full mode (zoom 0-12): Complete GL_TEXTURE_2D of size 2^zoom x 2^zoom
 * - Windowed mode (zoom 13+): Fixed 512x512 texture with offset, centered
 *   on camera position. Tiles outside the window are not representable.
 *
 * Each texel stores a uint16 layer index, or kInvalidLayer (0xFFFF) sentinel.
 * The shader uses texelFetch with integer coordinates for O(1) lookup.
 *
 * Thread Safety: NOT thread-safe — GL thread only.
 */

#include <earth_map/math/tile_mathematics.h>
#include <glm/vec2.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace earth_map {

class IndirectionTextureManager {
public:
    static constexpr int kMaxFullIndirectionZoom = 12;
    static constexpr std::uint16_t kInvalidLayer = 0xFFFF;
    static constexpr std::uint32_t kWindowSize = 512;

    /**
     * @brief Constructor
     * @param skip_gl_init Skip OpenGL calls (for testing)
     */
    explicit IndirectionTextureManager(bool skip_gl_init = false);
    ~IndirectionTextureManager();

    IndirectionTextureManager(const IndirectionTextureManager&) = delete;
    IndirectionTextureManager& operator=(const IndirectionTextureManager&) = delete;
    IndirectionTextureManager(IndirectionTextureManager&&) = delete;
    IndirectionTextureManager& operator=(IndirectionTextureManager&&) = delete;

    /**
     * @brief Set the pool layer index for a tile
     *
     * Lazily creates the indirection texture for the zoom level if needed.
     * For windowed mode (zoom > 12), the tile must fall within the current
     * window; otherwise the call is silently ignored.
     */
    void SetTileLayer(const TileCoordinates& coords, std::uint16_t layer_index);

    /**
     * @brief Clear a tile entry (reset to kInvalidLayer)
     *
     * Called when a tile is evicted from the pool. No-op if zoom level
     * has no indirection texture or tile is outside window.
     */
    void ClearTile(const TileCoordinates& coords);

    /**
     * @brief Get the stored layer index for a tile (for testing/debugging)
     *
     * @return Layer index, or kInvalidLayer if not set / outside window
     */
    std::uint16_t GetTileLayer(const TileCoordinates& coords) const;

    /**
     * @brief Get GL texture ID for a zoom level
     * @return Texture ID, or 0 if not allocated
     */
    std::uint32_t GetTextureID(int zoom) const;

    /**
     * @brief Get window offset for a zoom level
     *
     * For full mode (zoom <= 12), returns (0, 0).
     * For windowed mode, returns the offset that the shader must subtract
     * from tile coordinates before texelFetch.
     */
    glm::ivec2 GetWindowOffset(int zoom) const;

    /**
     * @brief Update window center for a windowed zoom level
     *
     * Re-centers the indirection window around the given tile position.
     * If the new center is far from the old one, the texture is cleared
     * and all data must be re-uploaded. If close, data is preserved.
     *
     * No-op for full-mode zoom levels (0-12).
     */
    void UpdateWindowCenter(int zoom, int center_tile_x, int center_tile_y);

    /**
     * @brief Get all zoom levels that have allocated indirection textures
     */
    std::vector<int> GetActiveZoomLevels() const;

    /**
     * @brief Release/destroy indirection texture for a zoom level
     */
    void ReleaseZoomLevel(int zoom);

private:
    struct ZoomTexture {
        std::uint32_t texture_id = 0;
        int zoom = -1;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool windowed = false;
        glm::ivec2 window_offset{0, 0};

        // CPU-side mirror for skip_gl_init mode and fast reads
        std::vector<std::uint16_t> data;
    };

    bool IsWindowedMode(int zoom) const { return zoom > kMaxFullIndirectionZoom; }
    void CreateZoomTexture(int zoom);
    void ClearZoomTextureData(ZoomTexture& zt);

    /**
     * @brief Shift window data by (dx, dy), preserving overlapping tiles
     *
     * Moves existing data so that tiles in the overlap region end up at
     * their correct new texel positions. Newly exposed texels are set to
     * kInvalidLayer.
     */
    void ShiftWindowData(ZoomTexture& zt, int dx, int dy);

    /**
     * @brief Check if tile coords fall within the windowed texture
     */
    bool IsTileInWindow(const ZoomTexture& zt, int tile_x, int tile_y) const;

    /**
     * @brief Convert tile coords to texel position in the indirection texture
     *
     * For full mode: texel = (tile_x, tile_y)
     * For windowed mode: texel = (tile_x - offset_x, tile_y - offset_y)
     */
    glm::ivec2 TileToTexel(const ZoomTexture& zt, int tile_x, int tile_y) const;

    std::unordered_map<int, ZoomTexture> zoom_textures_;
    bool skip_gl_init_;
    std::uint32_t dummy_texture_id_ = 0;
};

} // namespace earth_map
