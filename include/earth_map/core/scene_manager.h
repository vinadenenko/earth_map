#pragma once

/**
 * @file scene_manager.h
 * @brief Scene graph management
 * 
 * Provides scene graph management for organizing and managing
 * all renderable objects in the 3D scene.
 */

#include <memory>
#include <vector>
#include <string>

namespace earth_map {

// Forward declarations
struct Configuration;
class Renderer;
class CameraController;

/**
 * @brief Scene manager for organizing renderable objects
 * 
 * Manages the scene graph, object visibility, and spatial organization.
 * Provides efficient culling and rendering organization.
 */
class SceneManager {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~SceneManager() = default;
    
    /**
     * @brief Initialize scene manager
     * 
     * @param renderer Renderer to use for scene management
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize(Renderer* renderer) = 0;
    
    /**
     * @brief Update scene (called once per frame)
     * 
     * Updates all scene objects, performs culling, and prepares render lists.
     */
    virtual void Update() = 0;
    
    /**
     * @brief Load data from file
     * 
     * @param file_path Path to data file
     * @return true if loading succeeded, false otherwise
     */
    virtual bool LoadData(const std::string& file_path) = 0;
    
    /**
     * @brief Clear all scene data
     */
    virtual void Clear() = 0;
    
    /**
     * @brief Get number of objects in scene
     * 
     * @return std::size_t Number of objects
     */
    virtual std::size_t GetObjectCount() const = 0;

protected:
    /**
     * @brief Protected constructor
     */
    SceneManager() = default;
};

/**
 * @brief Factory function to create scene manager instance
 * 
 * @param config Configuration parameters
 * @return SceneManager* New scene manager instance (caller owns)
 */
SceneManager* CreateSceneManager(const Configuration& config);

} // namespace earth_map