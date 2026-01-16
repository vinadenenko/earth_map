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
    std::string GetPerformanceStats() const override;

private:
    Configuration config_;                     ///< Configuration parameters
    std::unique_ptr<Renderer> renderer_;      ///< Rendering engine
    std::unique_ptr<SceneManager> scene_manager_; ///< Scene management
    std::unique_ptr<CameraController> camera_controller_; ///< Camera control
    bool initialized_ = false;                 ///< Initialization status
    
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