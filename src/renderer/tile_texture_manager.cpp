/**
 * @file tile_texture_manager.cpp
 * @brief Tile texture manager implementation with OpenGL support
 */

#include <earth_map/renderer/tile_texture_manager.h>
#include <earth_map/data/tile_cache.h>
#include <earth_map/data/tile_loader.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
// #include <glad/glad.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <unordered_set>
#include <queue>
#include <thread>
#include <future>
// #include <GL/gl.h>
#include <GL/glew.h>

namespace earth_map {

// TextureAtlas implementation
glm::vec4 TextureAtlas::GetTileUV(std::size_t slot) const {
    if (grid_width == 0 || grid_height == 0 || slot >= occupied.size()) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    
    std::size_t row = slot / grid_width;
    std::size_t col = slot % grid_width;
    
    float u1 = static_cast<float>(col * tile_size) / width;
    float v1 = static_cast<float>(row * tile_size) / height;
    float u2 = static_cast<float>((col + 1) * tile_size) / width;
    float v2 = static_cast<float>((row + 1) * tile_size) / height;
    
    return glm::vec4(u1, v1, u2, v2);
}

glm::vec4 TextureAtlas::GetTileUV(const TileCoordinates& coords) const {
    auto it = tile_slots.find(coords);
    if (it != tile_slots.end()) {
        return GetTileUV(it->second);
    }
    
    return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
}

/**
 * @brief Basic tile texture manager implementation
 */
class BasicTileTextureManager : public TileTextureManager {
public:
    explicit BasicTileTextureManager(const TileTextureManagerConfig& config) 
        : config_(config) {}
    
    ~BasicTileTextureManager() override {
        Cleanup();
    }
    
    bool Initialize(const TileTextureManagerConfig& config) override;
    void SetTileCache(std::shared_ptr<TileCache> cache) override;
    void SetTileLoader(std::shared_ptr<TileLoader> loader) override;
    
    std::uint32_t GetTexture(const TileCoordinates& coordinates) override;
    
    std::future<bool> LoadTextureAsync(
        const TileCoordinates& coordinates,
        TileTextureCallback callback = nullptr) override;
    
    std::vector<std::future<bool>> LoadTexturesAsync(
        const std::vector<TileCoordinates>& coordinates,
        TileTextureCallback callback = nullptr) override;
    
    bool UpdateTexture(const TileCoordinates& coordinates,
                     const TileData& tile_data) override;
    
    void BindTexture(std::uint32_t texture_id, 
                    std::uint32_t texture_unit = 0) override;
    
    glm::vec4 GetTileUV(const TileCoordinates& coordinates) override;
    
    bool IsTextureAvailable(const TileCoordinates& coordinates) override;
    
    std::size_t PreloadVisibleTiles(
        const std::vector<TileCoordinates>& visible_tiles) override;
    
    std::size_t EvictUnusedTextures(bool force_eviction = false) override;
    
    TileTextureManagerStats GetStatistics() const override;
    
    TileTextureManagerConfig GetConfiguration() const override { return config_; }
    bool SetConfiguration(const TileTextureManagerConfig& config) override;
    
    void Update() override;
    void Cleanup() override;

private:
    TileTextureManagerConfig config_;
    std::shared_ptr<TileCache> tile_cache_;
    std::shared_ptr<TileLoader> tile_loader_;
    
    // Texture storage
    mutable std::mutex texture_mutex_;
    std::unordered_map<TileCoordinates, std::shared_ptr<TextureTile>, TileCoordinatesHash> textures_;
    std::vector<std::unique_ptr<TextureAtlas>> atlases_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    TileTextureManagerStats stats_;
    
    // State tracking
    std::atomic<std::uint32_t> uploads_this_frame_{0};
    std::unordered_set<TileCoordinates, TileCoordinatesHash> loading_textures_;
    
    // Internal methods
    std::uint32_t CreateOpenGLTexture();
    std::uint32_t UpdateOpenGLTexture(std::uint32_t texture_id, 
                                     const std::vector<std::uint8_t>& data,
                                     std::uint32_t width, std::uint32_t height,
                                     std::uint8_t channels);
    std::uint32_t CreateTextureAtlas();
    std::size_t FindAtlasSlot();
    bool AddTileToAtlas(const TileCoordinates& coords, 
                       const std::vector<std::uint8_t>& data,
                       std::uint32_t width, std::uint32_t height,
                       std::uint8_t channels);
    void SetTextureParameters(std::uint32_t texture_id);
    bool LoadImageData(const std::vector<std::uint8_t>& image_data,
                      std::vector<std::uint8_t>& pixel_data,
                      std::uint32_t& width, std::uint32_t& height,
                      std::uint8_t& channels);
    void UpdateMemoryUsage(std::int64_t delta_bytes);
    void UpdateStatistics();
    TileCoordinatesHash hasher_;
};

// Factory function
std::unique_ptr<TileTextureManager> CreateTileTextureManager(
    const TileTextureManagerConfig& config) {
    return std::make_unique<BasicTileTextureManager>(config);
}

bool BasicTileTextureManager::Initialize(const TileTextureManagerConfig& config) {
    config_ = config;
    
    // Initialize OpenGL context requirements
    glEnable(GL_TEXTURE_2D);
    
    // Set anisotropic filtering if available
    if (config_.anisotropic_level > 0) {
        GLfloat max_anisotropy = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_anisotropy);
        if (max_anisotropy > 0.0f) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 
                          std::min(static_cast<float>(config_.anisotropic_level), max_anisotropy));
        }
    }
    
    spdlog::info("Tile texture manager initialized with max {} textures and {} MB memory limit",
                config_.max_textures, config_.max_texture_memory_mb);
    
    return true;
}

void BasicTileTextureManager::SetTileCache(std::shared_ptr<TileCache> cache) {
    tile_cache_ = cache;
}

void BasicTileTextureManager::SetTileLoader(std::shared_ptr<TileLoader> loader) {
    tile_loader_ = loader;
}

std::uint32_t BasicTileTextureManager::GetTexture(const TileCoordinates& coordinates) {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    
    auto it = textures_.find(coordinates);
    if (it != textures_.end() && it->second->is_valid) {
        // Update last used time
        it->second->last_used = std::chrono::steady_clock::now();
        return it->second->texture_id;
    }
    
    // Check if texture is in atlas
    for (const auto& atlas : atlases_) {
        auto slot_it = atlas->tile_slots.find(coordinates);
        if (slot_it != atlas->tile_slots.end()) {
            return atlas->atlas_id;
        }
    }
    
    return 0; // Texture not available
}

std::future<bool> BasicTileTextureManager::LoadTextureAsync(
    const TileCoordinates& coordinates,
    TileTextureCallback callback) {

    spdlog::info("TextureManager: LoadTextureAsync called for {}/{}/{}",
                 coordinates.x, coordinates.y, coordinates.zoom);

    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    
    // Check if already loading
    if (loading_textures_.find(coordinates) != loading_textures_.end()) {
        promise->set_value(false);
        return future;
    }
    
    loading_textures_.insert(coordinates);
    
    // Check cache first
    if (tile_cache_) {
        auto cached_tile = tile_cache_->Retrieve(coordinates);
        if (cached_tile && cached_tile->IsValid()) {
            bool success = UpdateTexture(coordinates, *cached_tile);
            
            if (callback) {
                auto texture_id = GetTexture(coordinates);
                callback(coordinates, texture_id);
            }
            
            loading_textures_.erase(coordinates);
            promise->set_value(success);
            return future;
        }
    }
    
    // Load from tile loader
    if (tile_loader_) {
        auto load_future = tile_loader_->LoadTileAsync(coordinates, 
            [this, coordinates, callback, promise](const TileLoadResult& result) {
                bool success = false;
                
                if (result.success && result.tile_data) {
                    success = UpdateTexture(coordinates, *result.tile_data);
                    
                    if (callback && success) {
                        auto texture_id = GetTexture(coordinates);
                        callback(coordinates, texture_id);
                    }
                }
                
                loading_textures_.erase(coordinates);
                promise->set_value(success);
            });
        
        return future;
    }
    
    loading_textures_.erase(coordinates);
    promise->set_value(false);
    return future;
}

std::vector<std::future<bool>> BasicTileTextureManager::LoadTexturesAsync(
    const std::vector<TileCoordinates>& coordinates,
    TileTextureCallback callback) {
    
    std::vector<std::future<bool>> futures;
    
    for (const auto& coords : coordinates) {
        futures.push_back(LoadTextureAsync(coords, callback));
    }
    
    return futures;
}

bool BasicTileTextureManager::UpdateTexture(const TileCoordinates& coordinates,
                                         const TileData& tile_data) {
    if (tile_data.data.empty()) {
        return false;
    }
    
    // Load image data
    std::vector<std::uint8_t> pixel_data;
    std::uint32_t width, height;
    std::uint8_t channels;
    
    if (!LoadImageData(tile_data.data, pixel_data, width, height, channels)) {
        spdlog::warn("Failed to load image data for tile {}/{}/{}", 
                    coordinates.x, coordinates.y, coordinates.zoom);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(texture_mutex_);
    
    // Check if using texture atlas
    if (config_.use_texture_atlas && width == config_.tile_size && height == config_.tile_size) {
        if (AddTileToAtlas(coordinates, pixel_data, width, height, channels)) {
            UpdateMemoryUsage(width * height * channels);
            uploads_this_frame_++;
            return true;
        }
    }
    
    // Create or update individual texture
    auto it = textures_.find(coordinates);
    if (it != textures_.end()) {
        // Update existing texture
        UpdateOpenGLTexture(it->second->texture_id, pixel_data, width, height, channels);
        it->second->generation++;
        it->second->last_used = std::chrono::steady_clock::now();
        it->second->is_valid = true;
    } else {
        // Create new texture
        auto texture = std::make_shared<TextureTile>(coordinates);
        texture->texture_id = CreateOpenGLTexture();
        texture->width = width;
        texture->height = height;
        texture->channels = channels;
        texture->is_valid = true;
        
        UpdateOpenGLTexture(texture->texture_id, pixel_data, width, height, channels);
        
        textures_[coordinates] = texture;
        UpdateMemoryUsage(width * height * channels);
    }
    
    uploads_this_frame_++;
    return true;
}

void BasicTileTextureManager::BindTexture(std::uint32_t texture_id, 
                                        std::uint32_t texture_unit) {
    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindTexture(GL_TEXTURE_2D, texture_id);
}

glm::vec4 BasicTileTextureManager::GetTileUV(const TileCoordinates& coordinates) {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    
    // Check if texture is in atlas
    for (const auto& atlas : atlases_) {
        auto slot_it = atlas->tile_slots.find(coordinates);
        if (slot_it != atlas->tile_slots.end()) {
            return atlas->GetTileUV(coordinates);
        }
    }
    
    // For individual textures, return full UV range
    return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
}

bool BasicTileTextureManager::IsTextureAvailable(const TileCoordinates& coordinates) {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    
    auto it = textures_.find(coordinates);
    if (it != textures_.end() && it->second->is_valid) {
        return true;
    }
    
    // Check atlases
    for (const auto& atlas : atlases_) {
        if (atlas->tile_slots.find(coordinates) != atlas->tile_slots.end()) {
            return true;
        }
    }
    
    return false;
}

std::size_t BasicTileTextureManager::PreloadVisibleTiles(
    const std::vector<TileCoordinates>& visible_tiles) {
    
    std::size_t preloaded_count = 0;
    
    for (const auto& coords : visible_tiles) {
        if (!IsTextureAvailable(coords)) {
            auto future = LoadTextureAsync(coords);
            // We don't wait for completion here, just initiate loading
            preloaded_count++;
        }
    }
    
    spdlog::debug("Initiated loading for {} visible tiles", preloaded_count);
    return preloaded_count;
}

std::size_t BasicTileTextureManager::EvictUnusedTextures(bool force_eviction) {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    std::vector<TileCoordinates> to_evict;
    
    for (const auto& [coords, texture] : textures_) {
        bool should_evict = force_eviction;
        
        if (!should_evict) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - texture->last_used).count();
            
            should_evict = age > config_.eviction_timeout_seconds;
        }
        
        if (should_evict) {
            to_evict.push_back(coords);
        }
    }
    
    // Evict textures
    for (const auto& coords : to_evict) {
        auto it = textures_.find(coords);
        if (it != textures_.end()) {
            glDeleteTextures(1, &it->second->texture_id);
            UpdateMemoryUsage(-(static_cast<std::int64_t>(
                it->second->width * it->second->height * it->second->channels)));
            textures_.erase(it);
        }
    }
    
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.evictions_count += to_evict.size();
    
    return to_evict.size();
}

TileTextureManagerStats BasicTileTextureManager::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

bool BasicTileTextureManager::SetConfiguration(const TileTextureManagerConfig& config) {
    config_ = config;
    return true;
}

void BasicTileTextureManager::Update() {
    // Update statistics
    UpdateStatistics();
    
    // Reset frame counter
    uploads_this_frame_ = 0;
    
    // Evict unused textures if needed
    if (stats_.texture_memory_bytes > config_.max_texture_memory_mb * 1024 * 1024) {
        EvictUnusedTextures();
    }
}

void BasicTileTextureManager::Cleanup() {
    std::lock_guard<std::mutex> lock(texture_mutex_);
    
    // Clean up individual textures
    for (const auto& [coords, texture] : textures_) {
        if (texture->texture_id != 0) {
            glDeleteTextures(1, &texture->texture_id);
        }
    }
    textures_.clear();
    
    // Clean up atlases
    for (auto& atlas : atlases_) {
        if (atlas->atlas_id != 0) {
            glDeleteTextures(1, &atlas->atlas_id);
        }
    }
    atlases_.clear();
    
    // Reset statistics
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.Reset();
}

// Private implementation methods

std::uint32_t BasicTileTextureManager::CreateOpenGLTexture() {
    std::uint32_t texture_id;
    glGenTextures(1, &texture_id);
    
    SetTextureParameters(texture_id);
    
    return texture_id;
}

std::uint32_t BasicTileTextureManager::UpdateOpenGLTexture(std::uint32_t texture_id, 
                                                         const std::vector<std::uint8_t>& data,
                                                         std::uint32_t width, std::uint32_t height,
                                                         std::uint8_t channels) {
    glBindTexture(GL_TEXTURE_2D, texture_id);
    
    GLenum format = GL_RGB;
    GLenum internal_format = GL_RGB8;
    
    if (channels == 4) {
        format = GL_RGBA;
        internal_format = GL_RGBA8;
    } else if (channels == 1) {
        format = GL_RED;
        internal_format = GL_R8;
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, 
                 format, GL_UNSIGNED_BYTE, data.data());
    
    if (config_.filter_mode == TileTextureManagerConfig::FilterMode::MIPMAP) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    
    return texture_id;
}

std::uint32_t BasicTileTextureManager::CreateTextureAtlas() {
    auto atlas = std::make_unique<TextureAtlas>();
    atlas->width = config_.atlas_size;
    atlas->height = config_.atlas_size;
    atlas->tile_size = config_.tile_size;
    atlas->grid_width = atlas->width / atlas->tile_size;
    atlas->grid_height = atlas->height / atlas->tile_size;
    
    atlas->occupied.resize(atlas->grid_width * atlas->grid_height, false);
    atlas->free_slots.reserve(atlas->grid_width * atlas->grid_height);
    
    // Initialize free slots
    for (std::size_t i = 0; i < atlas->occupied.size(); ++i) {
        atlas->free_slots.push_back(i);
    }
    
    // Create OpenGL texture
    atlas->atlas_id = CreateOpenGLTexture();
    glBindTexture(GL_TEXTURE_2D, atlas->atlas_id);
    
    // Initialize with transparent data
    std::vector<std::uint8_t> transparent_data(atlas->width * atlas->height * 4, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas->width, atlas->height, 
                 0, GL_RGBA, GL_UNSIGNED_BYTE, transparent_data.data());
    
    std::uint32_t atlas_id = atlas->atlas_id;
    
    std::lock_guard<std::mutex> lock(texture_mutex_);
    atlases_.push_back(std::move(atlas));
    
    UpdateMemoryUsage(atlas->width * atlas->height * 4);
    
    return atlas_id;
}

std::size_t BasicTileTextureManager::FindAtlasSlot() {
    for (auto& atlas : atlases_) {
        if (!atlas->free_slots.empty()) {
            std::size_t slot = atlas->free_slots.back();
            atlas->free_slots.pop_back();
            return slot;
        }
    }
    
    // Create new atlas
    CreateTextureAtlas();
    
    if (!atlases_.empty()) {
        return FindAtlasSlot(); // Try again with new atlas
    }
    
    return 0; // Failed to create atlas
}

bool BasicTileTextureManager::AddTileToAtlas(const TileCoordinates& coords, 
                                            const std::vector<std::uint8_t>& data,
                                            std::uint32_t width, std::uint32_t height,
                                            std::uint8_t channels) {
    if (atlases_.empty()) {
        CreateTextureAtlas();
    }
    
    std::size_t slot = FindAtlasSlot();
    if (slot == 0 && atlases_.empty()) {
        return false;
    }
    
    // Find the atlas that contains this slot
    for (auto& atlas : atlases_) {
        if (slot < atlas->occupied.size()) {
            // Update atlas texture
            std::size_t row = slot / atlas->grid_width;
            std::size_t col = slot % atlas->grid_width;
            
            std::uint32_t x_offset = col * atlas->tile_size;
            std::uint32_t y_offset = row * atlas->tile_size;
            
            glBindTexture(GL_TEXTURE_2D, atlas->atlas_id);
            
            GLenum format = GL_RGB;
            if (channels == 4) format = GL_RGBA;
            else if (channels == 1) format = GL_RED;
            
            glTexSubImage2D(GL_TEXTURE_2D, 0, x_offset, y_offset, 
                           width, height, format, GL_UNSIGNED_BYTE, data.data());
            
            // Mark slot as occupied
            atlas->occupied[slot] = true;
            atlas->tile_slots[coords] = slot;
            
            if (config_.filter_mode == TileTextureManagerConfig::FilterMode::MIPMAP) {
                glGenerateMipmap(GL_TEXTURE_2D);
            }
            
            return true;
        }
    }
    
    return false;
}

void BasicTileTextureManager::SetTextureParameters(std::uint32_t texture_id) {
    glBindTexture(GL_TEXTURE_2D, texture_id);
    
    // Set filtering
    if (config_.filter_mode == TileTextureManagerConfig::FilterMode::NEAREST) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else if (config_.filter_mode == TileTextureManagerConfig::FilterMode::LINEAR) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else if (config_.filter_mode == TileTextureManagerConfig::FilterMode::MIPMAP) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    
    // Set wrapping
    GLint wrap_mode = (config_.wrap_mode == TileTextureManagerConfig::WrapMode::CLAMP) ? 
                     GL_CLAMP_TO_EDGE : GL_REPEAT;
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);
}

bool BasicTileTextureManager::LoadImageData(const std::vector<std::uint8_t>& image_data,
                                         std::vector<std::uint8_t>& pixel_data,
                                         std::uint32_t& width, std::uint32_t& height,
                                         std::uint8_t& channels) {
    int w, h, c;
    stbi_uc* pixels = stbi_load_from_memory(image_data.data(), 
                                          static_cast<int>(image_data.size()),
                                          &w, &h, &c, 0);
    
    if (!pixels) {
        return false;
    }
    
    width = static_cast<std::uint32_t>(w);
    height = static_cast<std::uint32_t>(h);
    channels = static_cast<std::uint8_t>(c);
    
    std::size_t total_pixels = width * height * channels;
    pixel_data.assign(pixels, pixels + total_pixels);
    
    stbi_image_free(pixels);
    return true;
}

void BasicTileTextureManager::UpdateMemoryUsage(std::int64_t delta_bytes) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.texture_memory_bytes += delta_bytes;
    
    if (stats_.texture_memory_bytes > stats_.peak_memory_bytes) {
        stats_.peak_memory_bytes = stats_.texture_memory_bytes;
    }
}

void BasicTileTextureManager::UpdateStatistics() {
    std::lock_guard<std::mutex> texture_lock(texture_mutex_);
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    
    stats_.total_textures = textures_.size();
    stats_.atlas_count = atlases_.size();
    stats_.atlas_tiles = 0;
    stats_.atlas_memory_bytes = 0;
    
    for (const auto& atlas : atlases_) {
        stats_.atlas_tiles += atlas->tile_slots.size();
        stats_.atlas_memory_bytes += atlas->width * atlas->height * 4; // RGBA
    }
    
    stats_.uploads_per_frame = uploads_this_frame_;
    
    // Calculate cache hit rate (placeholder)
    stats_.cache_hit_rate = 0.85f; // This would be calculated from actual usage
}

} // namespace earth_map
