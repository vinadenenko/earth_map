/**
 * @file texture_atlas_manager.cpp
 * @brief Implementation of texture atlas manager
 */

#include <earth_map/renderer/texture_atlas/texture_atlas_manager.h>
#include <GL/glew.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <limits>

namespace earth_map {

TextureAtlasManager::TextureAtlasManager(
    std::uint32_t atlas_width,
    std::uint32_t atlas_height,
    std::uint32_t tile_size,
    bool skip_gl_init)
    : atlas_width_(atlas_width)
    , atlas_height_(atlas_height)
    , tile_size_(tile_size)
    , skip_gl_init_(skip_gl_init) {

    // Calculate grid dimensions
    grid_width_ = atlas_width_ / tile_size_;
    grid_height_ = atlas_height_ / tile_size_;

    const std::size_t total_slots = grid_width_ * grid_height_;

    // Initialize slots
    slots_.reserve(total_slots);
    for (std::size_t i = 0; i < total_slots; ++i) {
        slots_.emplace_back(static_cast<int>(i));
        free_slots_.push(static_cast<int>(i));

        // Pre-calculate UV coordinates for each slot
        slots_[i].uv_coords = CalculateSlotUV(static_cast<int>(i));
    }

    // Create OpenGL atlas texture (unless skipped for testing)
    if (!skip_gl_init_) {
        CreateAtlasTexture();
    }

    spdlog::debug("TextureAtlasManager initialized: {}x{} atlas, {} tile size, {} total slots",
                  atlas_width_, atlas_height_, tile_size_, total_slots);
}

TextureAtlasManager::~TextureAtlasManager() {
    // Delete OpenGL texture if it was created
    if (atlas_texture_id_ != 0 && !skip_gl_init_) {
        glDeleteTextures(1, &atlas_texture_id_);
        atlas_texture_id_ = 0;
    }
}

void TextureAtlasManager::CreateAtlasTexture() {
    // Generate texture
    glGenTextures(1, &atlas_texture_id_);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_id_);

    // Allocate storage (empty, will be filled via glTexSubImage2D)
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB,
        atlas_width_,
        atlas_height_,
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        nullptr  // No initial data
    );

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Unbind
    glBindTexture(GL_TEXTURE_2D, 0);

    spdlog::debug("Created atlas texture: ID={}, size={}x{}",
                  atlas_texture_id_, atlas_width_, atlas_height_);
}

glm::vec4 TextureAtlasManager::CalculateSlotUV(int slot_index) const {
    if (slot_index < 0 || static_cast<std::size_t>(slot_index) >= slots_.size()) {
        return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // Calculate grid position
    const int col = slot_index % grid_width_;
    const int row = slot_index / grid_width_;

    // Calculate UV coordinates (normalized [0, 1])
    const float u_min = static_cast<float>(col) / static_cast<float>(grid_width_);
    const float v_min = static_cast<float>(row) / static_cast<float>(grid_height_);
    const float u_max = static_cast<float>(col + 1) / static_cast<float>(grid_width_);
    const float v_max = static_cast<float>(row + 1) / static_cast<float>(grid_height_);

    return glm::vec4(u_min, v_min, u_max, v_max);
}

int TextureAtlasManager::AllocateSlot() {
    // If free slots available, use one
    if (!free_slots_.empty()) {
        const int slot = free_slots_.front();
        free_slots_.pop();
        return slot;
    }

    // No free slots - evict LRU tile
    const int eviction_slot = FindEvictionCandidate();
    if (eviction_slot < 0) {
        spdlog::error("Failed to find eviction candidate (atlas empty but no free slots?)");
        return -1;
    }

    // Evict the tile in this slot
    AtlasSlot& slot_data = slots_[eviction_slot];
    if (slot_data.occupied) {
        spdlog::debug("Evicting tile {} from slot {} (LRU)",
                      slot_data.coords.GetKey(), eviction_slot);

        // Remove from lookup map
        coord_to_slot_.erase(slot_data.coords);

        // Mark as free
        slot_data.occupied = false;
    }

    return eviction_slot;
}

void TextureAtlasManager::FreeSlot(int slot_index) {
    if (slot_index < 0 || static_cast<std::size_t>(slot_index) >= slots_.size()) {
        return;
    }

    AtlasSlot& slot = slots_[slot_index];
    if (slot.occupied) {
        // Remove from lookup map
        coord_to_slot_.erase(slot.coords);

        // Mark as free
        slot.occupied = false;

        // Add to free list
        free_slots_.push(slot_index);
    }
}

int TextureAtlasManager::FindEvictionCandidate() const {
    int oldest_slot = -1;
    auto oldest_time = std::chrono::steady_clock::time_point::max();

    // Find slot with oldest last_used timestamp
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].occupied && slots_[i].last_used < oldest_time) {
            oldest_time = slots_[i].last_used;
            oldest_slot = static_cast<int>(i);
        }
    }

    return oldest_slot;
}

int TextureAtlasManager::UploadTile(
    const TileCoordinates& coords,
    const std::uint8_t* pixel_data,
    std::uint32_t width,
    std::uint32_t height,
    std::uint8_t channels) {

    if (!pixel_data) {
        spdlog::warn("UploadTile: null pixel data for tile {}", coords.GetKey());
        return -1;
    }

    if (width != tile_size_ || height != tile_size_) {
        spdlog::warn("UploadTile: tile size mismatch (expected {}x{}, got {}x{}) for tile {}",
                     tile_size_, tile_size_, width, height, coords.GetKey());
        return -1;
    }

    // Check if tile already in atlas (update in place)
    auto it = coord_to_slot_.find(coords);
    int slot_index;

    if (it != coord_to_slot_.end()) {
        // Tile exists - update in place
        slot_index = it->second;
        spdlog::trace("Updating existing tile {} in slot {}", coords.GetKey(), slot_index);
    } else {
        // Allocate new slot
        slot_index = AllocateSlot();
        if (slot_index < 0) {
            spdlog::error("Failed to allocate slot for tile {}", coords.GetKey());
            return -1;
        }

        // Update mappings
        coord_to_slot_[coords] = slot_index;
        slots_[slot_index].coords = coords;
        slots_[slot_index].occupied = true;

        spdlog::trace("Allocated slot {} for tile {}", slot_index, coords.GetKey());
    }

    // Update last used timestamp
    slots_[slot_index].last_used = std::chrono::steady_clock::now();

    // Upload to OpenGL (if not skipped)
    if (!skip_gl_init_ && atlas_texture_id_ != 0) {
        // Calculate pixel position in atlas
        const int col = slot_index % grid_width_;
        const int row = slot_index / grid_width_;
        const int x_offset = col * tile_size_;
        const int y_offset = row * tile_size_;

        // Bind atlas texture
        glBindTexture(GL_TEXTURE_2D, atlas_texture_id_);

        // Determine GL format based on channels
        GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;

        // Upload tile data to atlas sub-region
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            x_offset,
            y_offset,
            tile_size_,
            tile_size_,
            format,
            GL_UNSIGNED_BYTE,
            pixel_data
        );

        // Unbind
        glBindTexture(GL_TEXTURE_2D, 0);

        // Check for GL errors
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            spdlog::error("OpenGL error during tile upload: {}", error);
            return -1;
        }

        spdlog::trace("Uploaded tile {} to atlas at ({}, {})", coords.GetKey(), x_offset, y_offset);
    }

    return slot_index;
}

glm::vec4 TextureAtlasManager::GetTileUV(const TileCoordinates& coords) {
    // Find tile in atlas
    auto it = coord_to_slot_.find(coords);
    if (it == coord_to_slot_.end()) {
        // Tile not in atlas - return default UV
        return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const int slot_index = it->second;

    // Update last used timestamp (LRU tracking)
    slots_[slot_index].last_used = std::chrono::steady_clock::now();

    // Return cached UV coordinates
    return slots_[slot_index].uv_coords;
}

bool TextureAtlasManager::IsTileLoaded(const TileCoordinates& coords) const {
    return coord_to_slot_.find(coords) != coord_to_slot_.end();
}

void TextureAtlasManager::EvictTile(const TileCoordinates& coords) {
    auto it = coord_to_slot_.find(coords);
    if (it == coord_to_slot_.end()) {
        return;  // Tile not in atlas
    }

    const int slot_index = it->second;
    FreeSlot(slot_index);

    spdlog::debug("Evicted tile {} from slot {}", coords.GetKey(), slot_index);
}

} // namespace earth_map
