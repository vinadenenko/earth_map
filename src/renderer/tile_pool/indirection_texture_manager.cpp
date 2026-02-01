/**
 * @file indirection_texture_manager.cpp
 * @brief Per-zoom indirection texture implementation
 */

#include <earth_map/renderer/tile_pool/indirection_texture_manager.h>
#include <GL/glew.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>

namespace earth_map {

IndirectionTextureManager::IndirectionTextureManager(bool skip_gl_init)
    : skip_gl_init_(skip_gl_init) {
    spdlog::debug("IndirectionTextureManager initialized (skip_gl={})", skip_gl_init);
}

IndirectionTextureManager::~IndirectionTextureManager() {
    if (!skip_gl_init_) {
        for (auto& [zoom, zt] : zoom_textures_) {
            if (zt.texture_id != 0) {
                glDeleteTextures(1, &zt.texture_id);
            }
        }
    }
}

void IndirectionTextureManager::CreateZoomTexture(int zoom) {
    ZoomTexture zt;
    zt.zoom = zoom;
    zt.windowed = IsWindowedMode(zoom);

    if (zt.windowed) {
        zt.width = kWindowSize;
        zt.height = kWindowSize;
    } else {
        const auto dim = static_cast<std::uint32_t>(1u << zoom);
        zt.width = dim;
        zt.height = dim;
    }

    // Allocate CPU-side data initialized to kInvalidLayer
    zt.data.resize(zt.width * zt.height, kInvalidLayer);

    // Allocate GL texture
    if (!skip_gl_init_) {
        glGenTextures(1, &zt.texture_id);
        glBindTexture(GL_TEXTURE_2D, zt.texture_id);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_R16UI,
            static_cast<GLsizei>(zt.width),
            static_cast<GLsizei>(zt.height),
            0,
            GL_RED_INTEGER,
            GL_UNSIGNED_SHORT,
            zt.data.data());

        // Integer textures must use NEAREST filtering
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    spdlog::debug("Created indirection texture for zoom {}: {}x{} ({})",
                  zoom, zt.width, zt.height, zt.windowed ? "windowed" : "full");

    zoom_textures_[zoom] = std::move(zt);
}

void IndirectionTextureManager::ClearZoomTextureData(ZoomTexture& zt) {
    std::fill(zt.data.begin(), zt.data.end(), kInvalidLayer);

    if (!skip_gl_init_ && zt.texture_id != 0) {
        glBindTexture(GL_TEXTURE_2D, zt.texture_id);
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0,
            static_cast<GLsizei>(zt.width),
            static_cast<GLsizei>(zt.height),
            GL_RED_INTEGER, GL_UNSIGNED_SHORT,
            zt.data.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

bool IndirectionTextureManager::IsTileInWindow(
    const ZoomTexture& zt, int tile_x, int tile_y) const {

    if (!zt.windowed) {
        return tile_x >= 0 && tile_y >= 0 &&
               tile_x < static_cast<int>(zt.width) &&
               tile_y < static_cast<int>(zt.height);
    }

    const int local_x = tile_x - zt.window_offset.x;
    const int local_y = tile_y - zt.window_offset.y;

    return local_x >= 0 && local_y >= 0 &&
           local_x < static_cast<int>(zt.width) &&
           local_y < static_cast<int>(zt.height);
}

glm::ivec2 IndirectionTextureManager::TileToTexel(
    const ZoomTexture& zt, int tile_x, int tile_y) const {

    if (zt.windowed) {
        return {tile_x - zt.window_offset.x, tile_y - zt.window_offset.y};
    }
    return {tile_x, tile_y};
}

void IndirectionTextureManager::SetTileLayer(
    const TileCoordinates& coords, std::uint16_t layer_index) {

    const int zoom = coords.zoom;
    auto it = zoom_textures_.find(zoom);

    if (it == zoom_textures_.end()) {
        CreateZoomTexture(zoom);
        it = zoom_textures_.find(zoom);
    }

    ZoomTexture& zt = it->second;

    if (!IsTileInWindow(zt, coords.x, coords.y)) {
        return;  // Outside window — silently ignore
    }

    const glm::ivec2 texel = TileToTexel(zt, coords.x, coords.y);
    const std::size_t idx = texel.y * zt.width + texel.x;
    zt.data[idx] = layer_index;

    // Update GL texture
    if (!skip_gl_init_ && zt.texture_id != 0) {
        glBindTexture(GL_TEXTURE_2D, zt.texture_id);
        glTexSubImage2D(
            GL_TEXTURE_2D, 0,
            texel.x, texel.y, 1, 1,
            GL_RED_INTEGER, GL_UNSIGNED_SHORT,
            &layer_index);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void IndirectionTextureManager::ClearTile(const TileCoordinates& coords) {
    auto it = zoom_textures_.find(coords.zoom);
    if (it == zoom_textures_.end()) {
        return;
    }

    ZoomTexture& zt = it->second;

    if (!IsTileInWindow(zt, coords.x, coords.y)) {
        return;
    }

    const glm::ivec2 texel = TileToTexel(zt, coords.x, coords.y);
    const std::size_t idx = texel.y * zt.width + texel.x;
    zt.data[idx] = kInvalidLayer;

    if (!skip_gl_init_ && zt.texture_id != 0) {
        const std::uint16_t invalid = kInvalidLayer;
        glBindTexture(GL_TEXTURE_2D, zt.texture_id);
        glTexSubImage2D(
            GL_TEXTURE_2D, 0,
            texel.x, texel.y, 1, 1,
            GL_RED_INTEGER, GL_UNSIGNED_SHORT,
            &invalid);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

std::uint16_t IndirectionTextureManager::GetTileLayer(
    const TileCoordinates& coords) const {

    auto it = zoom_textures_.find(coords.zoom);
    if (it == zoom_textures_.end()) {
        return kInvalidLayer;
    }

    const ZoomTexture& zt = it->second;

    if (!IsTileInWindow(zt, coords.x, coords.y)) {
        return kInvalidLayer;
    }

    const glm::ivec2 texel = TileToTexel(zt, coords.x, coords.y);
    return zt.data[texel.y * zt.width + texel.x];
}

std::uint32_t IndirectionTextureManager::GetTextureID(int zoom) const {
    auto it = zoom_textures_.find(zoom);
    if (it == zoom_textures_.end()) {
        return 0;
    }
    return it->second.texture_id;
}

glm::ivec2 IndirectionTextureManager::GetWindowOffset(int zoom) const {
    auto it = zoom_textures_.find(zoom);
    if (it == zoom_textures_.end()) {
        return {0, 0};
    }
    return it->second.window_offset;
}

void IndirectionTextureManager::UpdateWindowCenter(
    int zoom, int center_tile_x, int center_tile_y) {

    if (!IsWindowedMode(zoom)) {
        return;  // Full mode — no windowing needed
    }

    const int half = static_cast<int>(kWindowSize / 2);
    const glm::ivec2 new_offset(center_tile_x - half, center_tile_y - half);

    auto it = zoom_textures_.find(zoom);
    if (it == zoom_textures_.end()) {
        // Create texture with this offset
        CreateZoomTexture(zoom);
        it = zoom_textures_.find(zoom);
        it->second.window_offset = new_offset;
        return;
    }

    ZoomTexture& zt = it->second;
    const glm::ivec2 old_offset = zt.window_offset;
    const glm::ivec2 delta = new_offset - old_offset;

    // If the shift is small enough that the old and new windows overlap
    // significantly, we could do a smart copy. For simplicity and correctness,
    // if the delta exceeds half the window size, clear everything.
    // Otherwise, keep data (tiles that are still in the window remain valid).
    if (std::abs(delta.x) > half || std::abs(delta.y) > half) {
        // Large jump — clear all data
        ClearZoomTextureData(zt);
        zt.window_offset = new_offset;
    } else {
        // Small shift — just update offset. Tiles that were within the old
        // window and are still within the new window retain their data because
        // we use absolute tile coords for lookup. Tiles that are now outside
        // need no explicit clearing since they won't be accessed.
        // However, tiles in the new area that weren't in the old window
        // need their texels cleared to kInvalidLayer.

        // Approach: clear the entire data and let callers re-set tile layers.
        // This is safe but suboptimal. At 512x512 x 2 bytes = 512KB, clearing
        // is fast (~0.1ms).
        //
        // For a more optimal approach, we'd only clear the newly exposed rows/cols.
        // Deferring that optimization.
        if (delta.x != 0 || delta.y != 0) {
            ClearZoomTextureData(zt);
        }
        zt.window_offset = new_offset;
    }
}

std::vector<int> IndirectionTextureManager::GetActiveZoomLevels() const {
    std::vector<int> levels;
    levels.reserve(zoom_textures_.size());
    for (const auto& [zoom, zt] : zoom_textures_) {
        levels.push_back(zoom);
    }
    std::sort(levels.begin(), levels.end());
    return levels;
}

void IndirectionTextureManager::ReleaseZoomLevel(int zoom) {
    auto it = zoom_textures_.find(zoom);
    if (it == zoom_textures_.end()) {
        return;
    }

    if (!skip_gl_init_ && it->second.texture_id != 0) {
        glDeleteTextures(1, &it->second.texture_id);
    }

    zoom_textures_.erase(it);
    spdlog::debug("Released indirection texture for zoom {}", zoom);
}

} // namespace earth_map
