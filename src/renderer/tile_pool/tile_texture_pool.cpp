/**
 * @file tile_texture_pool.cpp
 * @brief Implementation of GL_TEXTURE_2D_ARRAY tile texture pool
 */

#include <earth_map/renderer/tile_pool/tile_texture_pool.h>
#include <GL/glew.h>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace earth_map {

TileTexturePool::TileTexturePool(
    std::uint32_t tile_size,
    std::uint32_t max_layers,
    bool skip_gl_init)
    : tile_size_(tile_size)
    , max_layers_(max_layers)
    , skip_gl_init_(skip_gl_init) {

    layers_.reserve(max_layers_);
    for (std::uint32_t i = 0; i < max_layers_; ++i) {
        layers_.emplace_back(static_cast<int>(i));
        free_layers_.push(static_cast<int>(i));
    }

    if (!skip_gl_init_) {
        CreateTextureArray();
    }

    spdlog::debug("TileTexturePool initialized: {}x{} tile size, {} layers",
                  tile_size_, tile_size_, max_layers_);
}

TileTexturePool::~TileTexturePool() {
    if (texture_array_id_ != 0 && !skip_gl_init_) {
        glDeleteTextures(1, &texture_array_id_);
        texture_array_id_ = 0;
    }
}

void TileTexturePool::CreateTextureArray() {
    glGenTextures(1, &texture_array_id_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture_array_id_);

    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        GL_RGB8,
        static_cast<GLsizei>(tile_size_),
        static_cast<GLsizei>(tile_size_),
        static_cast<GLsizei>(max_layers_),
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        nullptr);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    spdlog::debug("Created texture array: ID={}, {}x{} x {} layers",
                  texture_array_id_, tile_size_, tile_size_, max_layers_);
}

int TileTexturePool::AllocateLayer() {
    if (!free_layers_.empty()) {
        const int layer = free_layers_.front();
        free_layers_.pop();
        return layer;
    }

    const int eviction_layer = FindEvictionCandidate();
    if (eviction_layer < 0) {
        spdlog::error("TileTexturePool: failed to find eviction candidate");
        return -1;
    }

    LayerSlot& slot = layers_[eviction_layer];
    if (slot.occupied) {
        spdlog::debug("TileTexturePool: evicting tile {} from layer {} (LRU)",
                      slot.coords.GetKey(), eviction_layer);
        coord_to_layer_.erase(slot.coords);
        slot.occupied = false;
    }

    return eviction_layer;
}

int TileTexturePool::FindEvictionCandidate() const {
    int oldest_layer = -1;
    auto oldest_time = std::chrono::steady_clock::time_point::max();

    for (std::size_t i = 0; i < layers_.size(); ++i) {
        if (layers_[i].occupied && layers_[i].last_used < oldest_time) {
            oldest_time = layers_[i].last_used;
            oldest_layer = static_cast<int>(i);
        }
    }

    return oldest_layer;
}

void TileTexturePool::FreeLayer(int layer_index) {
    if (layer_index < 0 || static_cast<std::size_t>(layer_index) >= layers_.size()) {
        return;
    }

    LayerSlot& slot = layers_[layer_index];
    if (slot.occupied) {
        coord_to_layer_.erase(slot.coords);
        slot.occupied = false;
        free_layers_.push(layer_index);
    }
}

int TileTexturePool::UploadTile(
    const TileCoordinates& coords,
    const std::uint8_t* pixel_data,
    std::uint32_t width,
    std::uint32_t height,
    std::uint8_t channels) {

    if (!pixel_data) {
        spdlog::warn("TileTexturePool::UploadTile: null pixel data for tile {}",
                     coords.GetKey());
        return -1;
    }

    if (width != tile_size_ || height != tile_size_) {
        spdlog::warn("TileTexturePool::UploadTile: size mismatch (expected {}x{}, got {}x{})",
                     tile_size_, tile_size_, width, height);
        return -1;
    }

    // Check if already loaded (update in place)
    auto it = coord_to_layer_.find(coords);
    int layer_index;

    if (it != coord_to_layer_.end()) {
        layer_index = it->second;
    } else {
        layer_index = AllocateLayer();
        if (layer_index < 0) {
            return -1;
        }
        coord_to_layer_[coords] = layer_index;
        layers_[layer_index].coords = coords;
        layers_[layer_index].occupied = true;
    }

    layers_[layer_index].last_used = std::chrono::steady_clock::now();

    // Upload to GL
    if (!skip_gl_init_ && texture_array_id_ != 0) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture_array_id_);

        const GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;

        glTexSubImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            0, 0, layer_index,
            static_cast<GLsizei>(tile_size_),
            static_cast<GLsizei>(tile_size_),
            1,
            format,
            GL_UNSIGNED_BYTE,
            pixel_data);

        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            spdlog::error("GL error during tile pool upload: {}", error);
            return -1;
        }
    }

    return layer_index;
}

void TileTexturePool::EvictTile(const TileCoordinates& coords) {
    auto it = coord_to_layer_.find(coords);
    if (it == coord_to_layer_.end()) {
        return;
    }
    FreeLayer(it->second);
}

bool TileTexturePool::IsTileLoaded(const TileCoordinates& coords) const {
    return coord_to_layer_.find(coords) != coord_to_layer_.end();
}

int TileTexturePool::GetLayerIndex(const TileCoordinates& coords) const {
    auto it = coord_to_layer_.find(coords);
    if (it == coord_to_layer_.end()) {
        return -1;
    }
    return it->second;
}

void TileTexturePool::TouchTile(const TileCoordinates& coords) {
    auto it = coord_to_layer_.find(coords);
    if (it != coord_to_layer_.end()) {
        layers_[it->second].last_used = std::chrono::steady_clock::now();
    }
}

} // namespace earth_map
