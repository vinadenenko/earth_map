#pragma once

/**
 * @file lod_manager.h
 * @brief Level of Detail (LOD) management system
 * 
 * Provides adaptive LOD selection based on camera distance, screen-space
 * error, and performance constraints for efficient rendering of
 * globe meshes and tiles.
 */

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <memory>
#include <array>

namespace earth_map {

// Forward declarations
struct GlobeTriangle;
struct Tile;
struct GlobeVertex;

/**
 * @brief LOD calculation parameters
 */
struct LODParams {
    /** Maximum screen-space error in pixels */
    float max_screen_error = 2.0f;
    
    /** Distance bias for LOD selection */
    float distance_bias = 1.0f;
    
    /** Performance target FPS */
    float target_fps = 60.0f;
    
    /** Maximum GPU time budget in milliseconds */
    float max_gpu_time = 16.67f;  // 1000ms / 60fps
    
    /** Enable adaptive LOD based on performance */
    bool enable_adaptive = true;
    
    /** Minimum LOD level */
    std::uint8_t min_lod = 0;
    
    /** Maximum LOD level */
    std::uint8_t max_lod = 8;
    
    /** LOD transition hysteresis (prevents flickering) */
    std::uint8_t hysteresis = 1;
};

/**
 * @brief LOD calculation result
 */
struct LODResult {
    /** Recommended LOD level */
    std::uint8_t lod_level = 0;
    
    /** Estimated screen-space error */
    float screen_error = 0.0f;
    
    /** Estimated GPU time required */
    float gpu_time_ms = 0.0f;
    
    /** Expected impact on performance */
    float performance_impact = 0.0f;
    
    /** Whether calculation was successful */
    bool valid = false;
    
    /**
     * @brief Default constructor
     */
    LODResult() = default;
    
    /**
     * @brief Construct with parameters
     */
    LODResult(std::uint8_t lod, float error, float gpu_time, float impact)
        : lod_level(lod), screen_error(error), gpu_time_ms(gpu_time), 
          performance_impact(impact), valid(true) {}
};

/**
 * @brief LOD manager interface
 * 
 * Manages level of detail selection for globe meshes and tiles
 * based on camera distance, screen-space error, and performance.
 */
class LODManager {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~LODManager() = default;
    
    /**
     * @brief Initialize LOD manager
     * 
     * @param params LOD calculation parameters
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize(const LODParams& params) = 0;
    
    /**
     * @brief Calculate optimal LOD level for globe triangle
     * 
     * @param triangle Globe triangle to evaluate
     * @param camera_position Current camera position
     * @param view_matrix Current view matrix
     * @param projection_matrix Current projection matrix
     * @param viewport_size Current viewport size
     * @return LODResult LOD calculation result
     */
    virtual LODResult CalculateTriangleLOD(
        const GlobeTriangle& triangle,
        const glm::vec3& camera_position,
        const glm::mat4& view_matrix,
        const glm::mat4& projection_matrix,
        const glm::vec2& viewport_size) const = 0;
    
    /**
     * @brief Calculate optimal LOD level for tile
     * 
     * @param tile Tile to evaluate
     * @param camera_position Current camera position
     * @param view_matrix Current view matrix
     * @param projection_matrix Current projection matrix
     * @param viewport_size Current viewport size
     * @return LODResult LOD calculation result
     */
    virtual LODResult CalculateTileLOD(
        const Tile& tile,
        const glm::vec3& camera_position,
        const glm::mat4& view_matrix,
        const glm::mat4& projection_matrix,
        const glm::vec2& viewport_size) const = 0;
    
    /**
     * @brief Update LOD manager statistics
     * 
     * Called once per frame to update internal statistics
     * and performance metrics.
     */
    virtual void Update() = 0;
    
    /**
     * @brief Get LOD manager statistics
     * 
     * @return std::pair<std::uint8_t, float> (current_lod_level, average_gpu_time)
     */
    virtual std::pair<std::uint8_t, float> GetStatistics() const = 0;
    
    /**
     * @brief Reset LOD manager statistics
     */
    virtual void Reset() = 0;
    
    /**
     * @brief Get LOD parameters
     * 
     * @return LODParams Current LOD parameters
     */
    virtual LODParams GetParameters() const = 0;
    
    /**
     * @brief Set LOD parameters
     * 
     * @param params New LOD parameters
     * @return true if parameters were applied, false otherwise
     */
    virtual bool SetParameters(const LODParams& params) = 0;

protected:
    /**
     * @brief Protected constructor
     */
    LODManager() = default;
    
    /**
     * @brief Calculate screen-space error for geometry
     * 
     * @param world_position Position in world space
     * @param view_matrix Current view matrix
     * @param projection_matrix Current projection matrix
     * @param viewport_size Current viewport size
     * @return float Screen-space error in pixels
     */
    virtual float CalculateScreenSpaceError(
        const glm::vec3& world_position,
        const glm::mat4& view_matrix,
        const glm::mat4& projection_matrix,
        const glm::vec2& viewport_size) const = 0;
    
    /**
     * @brief Estimate GPU rendering time for geometry
     * 
     * @param triangle_count Number of triangles to render
     * @param vertex_count Number of vertices to render
     * @param lod_level Current LOD level
     * @return float Estimated GPU time in milliseconds
     */
    virtual float EstimateGPUTime(std::size_t triangle_count,
                                std::size_t vertex_count,
                                std::uint8_t lod_level) const = 0;
    
    /**
     * @brief Calculate distance-based LOD level
     * 
     * @param distance Distance from camera to object
     * @return std::uint8_t Recommended LOD level
     */
    virtual std::uint8_t CalculateDistanceBasedLOD(float distance) const = 0;
    
    /**
     * @brief Apply hysteresis to LOD transitions
     * 
     * @param current_lod Current LOD level
     * @param target_lod Target LOD level
     * @return std::uint8_t LOD level after hysteresis
     */
    virtual std::uint8_t ApplyHysteresis(std::uint8_t current_lod,
                                         std::uint8_t target_lod) const = 0;
};

/**
 * @brief Factory function to create LOD manager
 * 
 * @param params LOD calculation parameters
 * @return std::unique_ptr<LODManager> New LOD manager instance
 */
std::unique_ptr<LODManager> CreateLODManager(const LODParams& params = {});

/**
 * @brief Basic LOD manager implementation
 * 
 * Implements screen-space error calculation, distance-based LOD,
 * and performance-adaptive LOD selection.
 */
class BasicLODManager : public LODManager {
public:
    explicit BasicLODManager(const LODParams& params = {});
    ~BasicLODManager() override = default;
    
    bool Initialize(const LODParams& params) override;
    LODResult CalculateTriangleLOD(const GlobeTriangle& triangle,
                                const glm::vec3& camera_position,
                                const glm::mat4& view_matrix,
                                const glm::mat4& projection_matrix,
                                const glm::vec2& viewport_size) const override;
    
    LODResult CalculateTileLOD(const Tile& tile,
                            const glm::vec3& camera_position,
                            const glm::mat4& view_matrix,
                            const glm::mat4& projection_matrix,
                            const glm::vec2& viewport_size) const override;
    
    void Update() override;
    std::pair<std::uint8_t, float> GetStatistics() const override;
    void Reset() override;
    
    LODParams GetParameters() const override;
    bool SetParameters(const LODParams& params) override;

private:
    LODParams params_;
    
    // Statistics
    std::uint32_t total_calculations_ = 0;
    std::uint8_t current_lod_level_ = 0;
    float total_gpu_time_ = 0.0f;
    std::uint32_t frame_count_ = 0;
    float average_gpu_time_ = 0.0f;
    
    // Performance metrics
    float last_frame_time_ = 0.0f;
    std::uint8_t hysteresis_state_ = 0;
    
    /**
     * @brief Update internal statistics
     */
    void UpdateStatistics(float gpu_time, std::uint8_t lod_level);
    
    /**
     * @brief Calculate LOD transition with performance consideration
     */
    std::uint8_t CalculatePerformanceBasedLOD(std::uint8_t target_lod, 
                                            float performance_impact) const;
    
    // Implementation of pure virtual methods from base class
    float CalculateScreenSpaceError(
        const glm::vec3& world_position,
        const glm::mat4& view_matrix,
        const glm::mat4& projection_matrix,
        const glm::vec2& viewport_size) const override;
    
    float EstimateGPUTime(std::size_t triangle_count,
                        std::size_t vertex_count,
                        std::uint8_t lod_level) const override;
    
    std::uint8_t CalculateDistanceBasedLOD(float distance) const override;
    
    std::uint8_t ApplyHysteresis(std::uint8_t current_lod,
                                 std::uint8_t target_lod) const override;
    
    // Static member for vertex access (placeholder)
    static std::vector<GlobeVertex> vertices_;
};

} // namespace earth_map