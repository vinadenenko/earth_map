#include "earth_map/renderer/tile_renderer.h"
#include <earth_map/core/earth_map_impl.h>
#include <earth_map/renderer/renderer.h>
#include <earth_map/core/scene_manager.h>
#include <earth_map/core/camera_controller.h>
#include <earth_map/platform/library_info.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace earth_map {

EarthMapImpl::EarthMapImpl(const Configuration& config) 
    : config_(config) {
    spdlog::info("Creating Earth Map instance v{}", LibraryInfo::GetVersion());
    
    if (!ValidateConfiguration(config)) {
        throw std::runtime_error("Invalid configuration parameters");
    }
}

EarthMapImpl::~EarthMapImpl() {
    spdlog::info("Destroying Earth Map instance");
    renderer_.reset();
    scene_manager_.reset();
    camera_controller_.reset();
}

bool EarthMapImpl::Initialize() {
    if (initialized_) {
        spdlog::warn("EarthMap already initialized");
        return true;
    }
    
    spdlog::info("Initializing Earth Map systems");
    
    try {
        if (!InitializeSubsystems()) {
            spdlog::error("Failed to initialize subsystems");
            return false;
        }
        
        initialized_ = true;
        spdlog::info("Earth Map initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Exception during initialization: {}", e.what());
        return false;
    }
}

void EarthMapImpl::Render() {
    if (!initialized_) {
        spdlog::error("EarthMap not initialized");
        return;
    }
    
    if (renderer_) {
        renderer_->Render();
    }
}

void EarthMapImpl::Resize(std::uint32_t width, std::uint32_t height) {
    config_.screen_width = width;
    config_.screen_height = height;
    
    if (renderer_ && initialized_) {
        renderer_->Resize(width, height);
    }
    
    spdlog::debug("Resized to {}x{}", width, height);
}

Renderer* EarthMapImpl::GetRenderer() {
    return renderer_.get();
}

SceneManager* EarthMapImpl::GetSceneManager() {
    return scene_manager_.get();
}

CameraController* EarthMapImpl::GetCameraController() {
    return camera_controller_.get();
}

bool EarthMapImpl::LoadData(const std::string& file_path) {
    if (!initialized_) {
        spdlog::error("EarthMap not initialized - cannot load data");
        return false;
    }
    
    spdlog::info("Loading data from: {}", file_path);
    
    // TODO: Implement data loading once scene manager and data parser are ready
    if (scene_manager_) {
        return scene_manager_->LoadData(file_path);
    }
    
    return false;
}

void EarthMapImpl::EnableMiniMap(bool enabled) {
    mini_map_enabled_ = enabled;
    if (renderer_) {
        renderer_->SetMiniMapEnabled(enabled);
    }
    spdlog::info("Mini-map display {}", enabled ? "enabled" : "disabled");
}

bool EarthMapImpl::IsMiniMapEnabled() const {
    return mini_map_enabled_;
}

void EarthMapImpl::SetMiniMapSize(uint32_t width, uint32_t height) {
    // TODO: Pass to mini-map renderer
    spdlog::info("Mini-map size set to {}x{}", width, height);
}

std::pair<uint32_t, uint32_t> EarthMapImpl::GetMiniMapSize() const {
    // TODO: Get from mini-map renderer
    return {256, 256}; // Default
}

void EarthMapImpl::SetMiniMapOffset(uint32_t offset_x, uint32_t offset_y) {
    // TODO: Pass to mini-map renderer
    spdlog::info("Mini-map offset set to ({}, {})", offset_x, offset_y);
}

std::pair<uint32_t, uint32_t> EarthMapImpl::GetMiniMapOffset() const {
    // TODO: Get from mini-map renderer
    return {10, 10}; // Default
}

std::string EarthMapImpl::GetPerformanceStats() const {
    if (!initialized_ || !renderer_) {
        return R"({"fps": 0, "frame_time_ms": 0, "draw_calls": 0})";
    }
    
    // TODO: Implement proper performance monitoring
    return R"({"fps": 60, "frame_time_ms": 16.67, "draw_calls": 1})";
}

bool EarthMapImpl::InitializeSubsystems() {
    spdlog::info("Initializing subsystems");
    
    try {
        // Initialize renderer first
        renderer_ = Renderer::Create(config_);
        if (!renderer_ || !renderer_->Initialize()) {
            spdlog::error("Failed to create or initialize renderer");
            return false;
        }
        
        // Initialize scene manager
        scene_manager_.reset(CreateSceneManager(config_));
        if (!scene_manager_ || !scene_manager_->Initialize(renderer_.get())) {
            spdlog::error("Failed to initialize scene manager");
            return false;
        }
        
        // Initialize camera controller
        camera_controller_.reset(CreateCameraController(config_));
        if (!camera_controller_ || !camera_controller_->Initialize()) {
            spdlog::error("Failed to initialize camera controller");
            return false;
        }

        renderer_->SetCameraController(camera_controller_.get());
        
        // Initialize tile texture coordinator (new lock-free architecture)
        // Create shared cache and loader for both tile manager and texture coordinator
        auto tile_cache = std::shared_ptr<TileCache>(CreateTileCache().release());
        // TODO: remove double config passing (constructor and Initialize)
        tile_cache->Initialize({});
        auto tile_loader = std::shared_ptr<TileLoader>(CreateTileLoader().release());
        tile_loader->Initialize({});

        // Set tile provider
        if (config_.tile_provider) {
            tile_loader->AddProvider(config_.tile_provider);
            tile_loader->SetDefaultProvider(config_.tile_provider->GetName());
        } else {
            tile_loader->AddProvider(TileProviders::OpenStreetMap);
            tile_loader->SetDefaultProvider("OpenStreetMap");
        }

        texture_coordinator_ = std::make_unique<TileTextureCoordinator>(
            tile_cache,
            tile_loader,
            4  // 4 worker threads for tile loading
        );

        spdlog::info("Tile texture coordinator initialized with lock-free architecture");

        // Connect tile system components
        auto tile_renderer = renderer_->GetTileRenderer();
        if (tile_renderer) {
            tile_renderer->SetTextureCoordinator(texture_coordinator_.get());
            spdlog::info("Tile system initialized with new lock-free texture coordinator");
        }
        
        spdlog::info("All subsystems initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Exception during subsystem initialization: {}", e.what());
        return false;
    }
}

bool EarthMapImpl::ValidateConfiguration(const Configuration& config) const {
    if (config.screen_width == 0 || config.screen_height == 0) {
        spdlog::error("Invalid screen dimensions: {}x{}", config.screen_width, config.screen_height);
        return false;
    }
    
    if (config.max_cache_memory_mb == 0) {
        spdlog::error("Invalid cache memory size: {} MB", config.max_cache_memory_mb);
        return false;
    }
    
    if (config.max_tile_count == 0) {
        spdlog::error("Invalid max tile count: {}", config.max_tile_count);
        return false;
    }
    
    spdlog::debug("Configuration validated successfully");
    return true;
}

} // namespace earth_map
