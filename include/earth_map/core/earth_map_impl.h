#pragma once

/**
 * @file earth_map_impl.h
 * @brief Implementation of the main Earth Map interface
 * 
 * Internal implementation class for the Earth Map library.
 * This header is not part of the public API.
 */

#include <earth_map/earth_map.h>
#include <earth_map/renderer/renderer.h>
#include <earth_map/core/scene_manager.h>
#include <earth_map/core/camera_controller.h>
#include <earth_map/data/tile_manager.h>
#include <earth_map/renderer/texture_atlas/tile_texture_coordinator.h>
#include <memory>

namespace earth_map {

/**
 * @brief Internal implementation of EarthMap interface
 * 
 * This class implements the EarthMap interface and manages all internal
 * subsystems including rendering, scene management, and data processing.
 */
class EarthMapImpl : public EarthMap {
public:
    /**
     * @brief Constructor
     * 
     * @param config Configuration parameters
     */
    explicit EarthMapImpl(const Configuration& config);
    
    /**
     * @brief Destructor
     */
    ~EarthMapImpl() override;
    
    // EarthMap interface implementation
    bool Initialize() override;
    void Render() override;
    void Resize(std::uint32_t width, std::uint32_t height) override;
    Renderer* GetRenderer() override;
    SceneManager* GetSceneManager() override;
    CameraController* GetCameraController() override;
    bool LoadData(const std::string& file_path) override;

    void EnableMiniMap(bool enabled) override;
    bool IsMiniMapEnabled() const override;
    void SetMiniMapSize(uint32_t width, uint32_t height) override;
    std::pair<uint32_t, uint32_t> GetMiniMapSize() const override;
    void SetMiniMapOffset(uint32_t offset_x, uint32_t offset_y) override;
    std::pair<uint32_t, uint32_t> GetMiniMapOffset() const override;
    std::string GetPerformanceStats() const override;

private:
    Configuration config_;                     ///< Configuration parameters
    std::unique_ptr<Renderer> renderer_;      ///< Rendering engine
    std::unique_ptr<SceneManager> scene_manager_; ///< Scene management
    std::unique_ptr<CameraController> camera_controller_; ///< Camera control
    std::unique_ptr<TileManager> tile_manager_; ///< Tile management
    std::unique_ptr<TileTextureCoordinator> texture_coordinator_; ///< Texture atlas coordinator (new lock-free architecture)
    bool initialized_ = false;                 ///< Initialization status
    bool mini_map_enabled_ = false;            ///< Mini-map display enabled
    
    /**
     * @brief Initialize subsystems
     * 
     * @return true if all subsystems initialized successfully
     */
    bool InitializeSubsystems();
    
    /**
     * @brief Validate configuration
     * 
     * @param config Configuration to validate
     * @return true if configuration is valid, false otherwise
     */
    bool ValidateConfiguration(const Configuration& config) const;
};

} // namespace earth_map
