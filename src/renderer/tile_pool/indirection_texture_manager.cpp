/**
 * @file indirection_texture_manager.cpp
 * @brief Source-aware imagery page-table implementation
 */

#include <earth_map/renderer/tile_pool/indirection_texture_manager.h>

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

namespace earth_map {
namespace {

std::size_t HashCombine(std::size_t seed, std::size_t value) noexcept {
    return seed ^ (value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U));
}

}  // namespace

IndirectionTextureManager::IndirectionTextureManager(bool skip_gl_init)
    : skip_gl_init_(skip_gl_init) {
    // A valid integer texture for table levels that are not resident yet.
    if (!skip_gl_init_) {
        glGenTextures(1, &dummy_texture_id_);
        glBindTexture(GL_TEXTURE_2D, dummy_texture_id_);

        const std::uint16_t sentinel = kInvalidLayer;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 1, 1, 0,
                     GL_RED_INTEGER, GL_UNSIGNED_SHORT, &sentinel);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

IndirectionTextureManager::~IndirectionTextureManager() {
    if (!skip_gl_init_) {
        for (auto& [identity, page_table] : page_tables_) {
            if (page_table.texture_id != 0) {
                glDeleteTextures(1, &page_table.texture_id);
            }
        }
        if (dummy_texture_id_ != 0) {
            glDeleteTextures(1, &dummy_texture_id_);
        }
    }
}

std::size_t IndirectionTextureManager::PageTableIdentityHash::operator()(
    const PageTableIdentity& identity) const noexcept {
    std::size_t seed = std::hash<std::string>{}(identity.imagery_source_id);
    seed = HashCombine(seed, std::hash<std::string>{}(identity.matrix_set_id));
    return HashCombine(seed, std::hash<std::uint32_t>{}(identity.level));
}

IndirectionTextureManager::PageTableIdentity IndirectionTextureManager::GetIdentity(
    const imagery::ImageTileKey& imagery_key) {
    return {
        imagery_key.imagery_source_id,
        imagery_key.matrix_set_id,
        imagery_key.address.level,
    };
}

glm::ivec2 IndirectionTextureManager::GetWindowOffset(
    const imagery::PageTableWindow& window) {
    return {
        static_cast<int>(window.origin_column),
        static_cast<int>(window.origin_row),
    };
}

void IndirectionTextureManager::CreatePageTable(
    const imagery::ImageTileKey& imagery_key) {
    if (!imagery_key.IsValid() ||
        imagery_key.address.level > imagery::TileMatrixSet::kMaximumSupportedLevel) {
        spdlog::error("IndirectionTextureManager: invalid imagery key for page table");
        return;
    }

    const PageTableIdentity identity = GetIdentity(imagery_key);
    if (page_tables_.contains(identity)) {
        return;
    }

    PageTableTexture page_table;
    const bool windowed = IsWindowedMode(identity.level);
    page_table.width = windowed ? kWindowSize : (std::uint32_t{1} << identity.level);
    page_table.height = page_table.width;
    page_table.window = {
        imagery::kVirtualImageryAddressContractVersion,
        next_window_generation_++,
        identity.imagery_source_id,
        identity.matrix_set_id,
        identity.level,
        0,
        0,
        page_table.width,
        page_table.height,
    };
    page_table.data.assign(
        static_cast<std::size_t>(page_table.width) * page_table.height,
        kInvalidLayer);

    if (!skip_gl_init_) {
        glGenTextures(1, &page_table.texture_id);
        glBindTexture(GL_TEXTURE_2D, page_table.texture_id);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_R16UI,
            static_cast<GLsizei>(page_table.width),
            static_cast<GLsizei>(page_table.height),
            0,
            GL_RED_INTEGER,
            GL_UNSIGNED_SHORT,
            page_table.data.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    page_tables_.emplace(identity, std::move(page_table));
}

void IndirectionTextureManager::ClearPageTableData(PageTableTexture& page_table) {
    std::fill(page_table.data.begin(), page_table.data.end(), kInvalidLayer);

    if (!skip_gl_init_ && page_table.texture_id != 0) {
        glBindTexture(GL_TEXTURE_2D, page_table.texture_id);
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            static_cast<GLsizei>(page_table.width),
            static_cast<GLsizei>(page_table.height),
            GL_RED_INTEGER,
            GL_UNSIGNED_SHORT,
            page_table.data.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

bool IndirectionTextureManager::IsTileInWindow(
    const PageTableTexture& page_table,
    const imagery::ImageTileKey& imagery_key) const {
    return page_table.window.TryGetTexel(imagery_key).has_value();
}

glm::ivec2 IndirectionTextureManager::TileToTexel(
    const PageTableTexture& page_table,
    const imagery::ImageTileKey& imagery_key) const {
    const auto texel = page_table.window.TryGetTexel(imagery_key);
    return texel.has_value() ? glm::ivec2(texel->x, texel->y) : glm::ivec2(-1, -1);
}

bool IndirectionTextureManager::SetTileLayer(
    const imagery::ImageTileKey& imagery_key,
    std::uint16_t layer_index) {
    if (!imagery_key.IsValid() || layer_index == kInvalidLayer) {
        return false;
    }

    const PageTableIdentity identity = GetIdentity(imagery_key);
    if (!page_tables_.contains(identity)) {
        CreatePageTable(imagery_key);
    }

    const auto it = page_tables_.find(identity);
    if (it == page_tables_.end() || !IsTileInWindow(it->second, imagery_key)) {
        return false;
    }

    PageTableTexture& page_table = it->second;
    const glm::ivec2 texel = TileToTexel(page_table, imagery_key);
    const std::size_t index =
        static_cast<std::size_t>(texel.y) * page_table.width + texel.x;
    page_table.data[index] = layer_index;

    if (!skip_gl_init_ && page_table.texture_id != 0) {
        glBindTexture(GL_TEXTURE_2D, page_table.texture_id);
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            texel.x,
            texel.y,
            1,
            1,
            GL_RED_INTEGER,
            GL_UNSIGNED_SHORT,
            &layer_index);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return true;
}

void IndirectionTextureManager::ClearTile(const imagery::ImageTileKey& imagery_key) {
    if (!imagery_key.IsValid()) {
        return;
    }

    const auto it = page_tables_.find(GetIdentity(imagery_key));
    if (it == page_tables_.end() || !IsTileInWindow(it->second, imagery_key)) {
        return;
    }

    PageTableTexture& page_table = it->second;
    const glm::ivec2 texel = TileToTexel(page_table, imagery_key);
    const std::size_t index =
        static_cast<std::size_t>(texel.y) * page_table.width + texel.x;
    page_table.data[index] = kInvalidLayer;

    if (!skip_gl_init_ && page_table.texture_id != 0) {
        const std::uint16_t invalid = kInvalidLayer;
        glBindTexture(GL_TEXTURE_2D, page_table.texture_id);
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            texel.x,
            texel.y,
            1,
            1,
            GL_RED_INTEGER,
            GL_UNSIGNED_SHORT,
            &invalid);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

std::uint16_t IndirectionTextureManager::GetTileLayer(
    const imagery::ImageTileKey& imagery_key) const {
    if (!imagery_key.IsValid()) {
        return kInvalidLayer;
    }

    const auto it = page_tables_.find(GetIdentity(imagery_key));
    if (it == page_tables_.end() || !IsTileInWindow(it->second, imagery_key)) {
        return kInvalidLayer;
    }

    const glm::ivec2 texel = TileToTexel(it->second, imagery_key);
    return it->second.data[
        static_cast<std::size_t>(texel.y) * it->second.width + texel.x];
}

std::uint32_t IndirectionTextureManager::GetTextureID(
    const imagery::ImageTileKey& imagery_key) const {
    if (!imagery_key.IsValid()) {
        return dummy_texture_id_;
    }

    const auto it = page_tables_.find(GetIdentity(imagery_key));
    return it == page_tables_.end() ? dummy_texture_id_ : it->second.texture_id;
}

glm::ivec2 IndirectionTextureManager::GetWindowOffset(
    const imagery::ImageTileKey& imagery_key) const {
    if (!imagery_key.IsValid()) {
        return {0, 0};
    }

    const auto it = page_tables_.find(GetIdentity(imagery_key));
    return it == page_tables_.end() ? glm::ivec2(0, 0) : GetWindowOffset(it->second.window);
}

std::optional<imagery::PageTableWindow> IndirectionTextureManager::GetPageTableWindow(
    const imagery::ImageTileKey& imagery_key) const {
    if (!imagery_key.IsValid()) {
        return std::nullopt;
    }

    const auto it = page_tables_.find(GetIdentity(imagery_key));
    return it == page_tables_.end()
        ? std::nullopt
        : std::optional<imagery::PageTableWindow>(it->second.window);
}

std::optional<IndirectionTextureManager::PageTableBinding>
IndirectionTextureManager::GetPageTableBinding(
    const imagery::ImageTileKey& imagery_key) const {
    if (!imagery_key.IsValid()) {
        return std::nullopt;
    }

    const auto it = page_tables_.find(GetIdentity(imagery_key));
    if (it == page_tables_.end()) {
        return std::nullopt;
    }

    return PageTableBinding{it->second.texture_id, it->second.window};
}

void IndirectionTextureManager::ShiftWindowData(
    PageTableTexture& page_table,
    int delta_x,
    int delta_y) {
    const int width = static_cast<int>(page_table.width);
    const int height = static_cast<int>(page_table.height);
    std::vector<std::uint16_t> shifted(
        static_cast<std::size_t>(width) * height,
        kInvalidLayer);

    const int source_x0 = std::max(0, delta_x);
    const int source_y0 = std::max(0, delta_y);
    const int source_x1 = std::min(width, width + delta_x);
    const int source_y1 = std::min(height, height + delta_y);

    for (int old_y = source_y0; old_y < source_y1; ++old_y) {
        const int new_y = old_y - delta_y;
        const int copy_width = source_x1 - source_x0;
        if (copy_width > 0) {
            std::memcpy(
                &shifted[static_cast<std::size_t>(new_y) * width + (source_x0 - delta_x)],
                &page_table.data[static_cast<std::size_t>(old_y) * width + source_x0],
                static_cast<std::size_t>(copy_width) * sizeof(std::uint16_t));
        }
    }
    page_table.data = std::move(shifted);
}

bool IndirectionTextureManager::UpdateWindowCenter(
    const imagery::ImageTileKey& center_tile) {
    if (!center_tile.IsValid() || !IsWindowedMode(center_tile.address.level)) {
        return false;
    }

    const PageTableIdentity identity = GetIdentity(center_tile);
    if (!page_tables_.contains(identity)) {
        CreatePageTable(center_tile);
    }

    const auto it = page_tables_.find(identity);
    if (it == page_tables_.end()) {
        return false;
    }

    PageTableTexture& page_table = it->second;
    imagery::PageTableWindow updated_window = page_table.window;
    const std::int64_t half_window = static_cast<std::int64_t>(kWindowSize / 2U);
    updated_window.origin_column =
        static_cast<std::int64_t>(center_tile.address.column) - half_window;
    updated_window.origin_row =
        static_cast<std::int64_t>(center_tile.address.row) - half_window;

    if (updated_window.origin_column == page_table.window.origin_column &&
        updated_window.origin_row == page_table.window.origin_row) {
        return false;
    }

    const std::int64_t delta_x =
        updated_window.origin_column - page_table.window.origin_column;
    const std::int64_t delta_y =
        updated_window.origin_row - page_table.window.origin_row;
    updated_window.generation = next_window_generation_++;

    if (std::abs(delta_x) >= static_cast<std::int64_t>(page_table.width) ||
        std::abs(delta_y) >= static_cast<std::int64_t>(page_table.height)) {
        ClearPageTableData(page_table);
    } else {
        ShiftWindowData(
            page_table,
            static_cast<int>(delta_x),
            static_cast<int>(delta_y));
        if (!skip_gl_init_ && page_table.texture_id != 0) {
            glBindTexture(GL_TEXTURE_2D, page_table.texture_id);
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                static_cast<GLsizei>(page_table.width),
                static_cast<GLsizei>(page_table.height),
                GL_RED_INTEGER,
                GL_UNSIGNED_SHORT,
                page_table.data.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    page_table.window = std::move(updated_window);
    return true;
}

std::vector<int> IndirectionTextureManager::GetActiveZoomLevels() const {
    std::vector<int> levels;
    levels.reserve(page_tables_.size());
    for (const auto& [identity, page_table] : page_tables_) {
        const int level = static_cast<int>(identity.level);
        if (std::find(levels.begin(), levels.end(), level) == levels.end()) {
            levels.push_back(level);
        }
    }
    std::sort(levels.begin(), levels.end());
    return levels;
}

void IndirectionTextureManager::ReleasePageTable(
    const imagery::ImageTileKey& imagery_key) {
    if (!imagery_key.IsValid()) {
        return;
    }

    const auto it = page_tables_.find(GetIdentity(imagery_key));
    if (it == page_tables_.end()) {
        return;
    }

    if (!skip_gl_init_ && it->second.texture_id != 0) {
        glDeleteTextures(1, &it->second.texture_id);
    }
    page_tables_.erase(it);
}

}  // namespace earth_map
