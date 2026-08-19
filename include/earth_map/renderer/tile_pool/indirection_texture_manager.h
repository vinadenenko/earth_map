#pragma once

/**
 * @file indirection_texture_manager.h
 * @brief Source-aware page tables mapping imagery keys to pool-layer indices
 *
 * Manages indirection textures that map canonical imagery keys to layer
 * indices in the TileTexturePool. Each table is owned by one imagery source,
 * matrix set, level, and PageTableWindow generation. Two modes:
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

#include <earth_map/imagery/tile_matrix_set.h>
#include <glm/vec2.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
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
     * @brief Set the pool layer index for a canonical imagery page
     *
     * Lazily creates the indirection texture for the zoom level if needed.
     * For windowed mode (zoom > 12), the tile must fall within the current
     * window; otherwise no entry is written and false is returned.
     */
    /**
     * @return true when the entry is represented by the current page-table
     * window; false when the tile is outside that window or cannot be stored.
     */
    bool SetTileLayer(const imagery::ImageTileKey& imagery_key, std::uint16_t layer_index);

    /**
     * @brief Clear an imagery-page entry (reset to kInvalidLayer)
     *
     * Called when a tile is evicted from the pool. No-op if zoom level
     * has no indirection texture or tile is outside window.
     */
    void ClearTile(const imagery::ImageTileKey& imagery_key);

    /**
     * @brief Get the stored layer index for an imagery page (testing/debugging)
     *
     * @return Layer index, or kInvalidLayer if not set / outside window
     */
    std::uint16_t GetTileLayer(const imagery::ImageTileKey& imagery_key) const;

    /**
     * @brief Get GL texture ID for an imagery page's page table
     * @return Texture ID, or 0 if not allocated
     */
    std::uint32_t GetTextureID(const imagery::ImageTileKey& imagery_key) const;

    /**
     * @brief Get window offset for an imagery page's page table
     *
     * For full mode (zoom <= 12), returns (0, 0).
     * For windowed mode, returns the offset that the shader must subtract
     * from tile coordinates before texelFetch.
     */
    glm::ivec2 GetWindowOffset(const imagery::ImageTileKey& imagery_key) const;

    /** Returns the immutable source-aware window used by an allocated table. */
    std::optional<imagery::PageTableWindow> GetPageTableWindow(
        const imagery::ImageTileKey& imagery_key) const;

    /**
     * @brief Update window center for a source-aware windowed page table
     *
     * Re-centers the indirection window around the given tile position.
     * If the new center is far from the old one, the texture is cleared
     * and all data must be re-uploaded. If close, data is preserved.
     *
     * No-op for full-mode zoom levels (0-12).
     */
    void UpdateWindowCenter(const imagery::ImageTileKey& center_tile);

    /**
     * @brief Get all levels that have allocated indirection textures
     */
    std::vector<int> GetActiveZoomLevels() const;

    /**
     * @brief Release/destroy the page table for an imagery source/matrix/level
     */
    void ReleasePageTable(const imagery::ImageTileKey& imagery_key);

private:
    struct PageTableIdentity {
        std::string imagery_source_id;
        std::string matrix_set_id;
        std::uint32_t level = 0;

        bool operator==(const PageTableIdentity& other) const noexcept = default;
    };

    struct PageTableIdentityHash {
        std::size_t operator()(const PageTableIdentity& identity) const noexcept;
    };

    struct PageTableTexture {
        std::uint32_t texture_id = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        imagery::PageTableWindow window;

        // CPU-side mirror for skip_gl_init mode and fast reads
        std::vector<std::uint16_t> data;
    };

    bool IsWindowedMode(std::uint32_t level) const {
        return level > static_cast<std::uint32_t>(kMaxFullIndirectionZoom);
    }
    static PageTableIdentity GetIdentity(const imagery::ImageTileKey& imagery_key);
    static glm::ivec2 GetWindowOffset(const imagery::PageTableWindow& window);
    void CreatePageTable(const imagery::ImageTileKey& imagery_key);
    void ClearPageTableData(PageTableTexture& page_table);

    /**
     * @brief Shift window data by (dx, dy), preserving overlapping tiles
     *
     * Moves existing data so that tiles in the overlap region end up at
     * their correct new texel positions. Newly exposed texels are set to
     * kInvalidLayer.
     */
    void ShiftWindowData(PageTableTexture& page_table, int dx, int dy);

    /**
     * @brief Check if tile coords fall within the windowed texture
     */
    bool IsTileInWindow(
        const PageTableTexture& page_table,
        const imagery::ImageTileKey& imagery_key) const;

    /**
     * @brief Convert tile coords to texel position in the indirection texture
     *
     * For full mode: texel = (tile_x, tile_y)
     * For windowed mode: texel = (tile_x - offset_x, tile_y - offset_y)
     */
    glm::ivec2 TileToTexel(
        const PageTableTexture& page_table,
        const imagery::ImageTileKey& imagery_key) const;

    std::unordered_map<PageTableIdentity, PageTableTexture, PageTableIdentityHash>
        page_tables_;
    std::uint64_t next_window_generation_ = 1;
    bool skip_gl_init_;
    std::uint32_t dummy_texture_id_ = 0;
};

} // namespace earth_map
