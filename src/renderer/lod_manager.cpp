/**
 * @file lod_manager.cpp
 * @brief Level of Detail management implementation
 */

#include <earth_map/renderer/lod_manager.h>
#include <earth_map/renderer/globe_mesh.h>
#include <earth_map/data/tile_manager.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <spdlog/spdlog.h>

namespace earth_map {

// Factory function
std::unique_ptr<LODManager> CreateLODManager(const LODParams& params) {
    return std::make_unique<BasicLODManager>(params);
}

BasicLODManager::BasicLODManager(const LODParams& params) 
    : params_(params) {
}

bool BasicLODManager::Initialize(const LODParams& params) {
    params_ = params;
    Reset();
    
    spdlog::info("Initializing LOD manager with max screen error: {}, distance bias: {}",
                 params_.max_screen_error, params_.distance_bias);
    
    return true;
}

float BasicLODManager::CalculateScreenSpaceError(
    const glm::vec3& world_position,
    const glm::mat4& view_matrix,
    const glm::mat4& projection_matrix,
    const glm::vec2& viewport_size) const {
    
    // Transform to clip space
    glm::vec4 clip_pos = projection_matrix * view_matrix * glm::vec4(world_position, 1.0f);
    
    // Perspective divide to get normalized device coordinates
    if (std::abs(clip_pos.w) > 1e-6f) {
        return 1000.0f;  // Behind camera, max error
    }
    
    glm::vec2 ndc = glm::vec2(clip_pos.x, clip_pos.y) / clip_pos.w;
    
    // Convert to screen coordinates
    glm::vec2 screen_pos = (ndc + glm::vec2(1.0f)) * 
                             glm::vec2(viewport_size.x * 0.5f, viewport_size.y * 0.5f);
    
    // Calculate screen-space error (distance from actual position)
    glm::vec2 ideal_screen_pos = screen_pos;  // Should be projected from mesh center
    glm::vec2 error_vector = screen_pos - ideal_screen_pos;
    
    return glm::length(error_vector);
}

float BasicLODManager::EstimateGPUTime(std::size_t triangle_count,
                                           std::size_t /* vertex_count */,
                                           std::uint8_t lod_level) const {
    
    // Base time per triangle at reference LOD (LOD 0)
    constexpr float base_triangle_time = 0.01f;  // 10 microseconds
    
    // Each LOD level increases/decreases geometry by factor of 4
    float lod_factor = std::pow(4.0f, static_cast<float>(lod_level));
    
    // GPU time scales with complexity
    return base_triangle_time * triangle_count * lod_factor;
}

std::uint8_t BasicLODManager::CalculateDistanceBasedLOD(float distance) const {
    // Distance thresholds for different LOD levels (in meters)
    constexpr std::array<float, 9> distance_thresholds = {
        500.0f,    // LOD 0: up to 500m
        1000.0f,   // LOD 1: up to 1km
        2000.0f,   // LOD 2: up to 2km
        5000.0f,   // LOD 3: up to 5km
        10000.0f,  // LOD 4: up to 10km
        20000.0f,  // LOD 5: up to 20km
        50000.0f,  // LOD 6: up to 50km
        100000.0f, // LOD 7: up to 100km
        200000.0f  // LOD 8: up to 200km
    };
    
    // Apply distance bias
    float adjusted_distance = distance * params_.distance_bias;
    
    for (std::uint8_t i = 0; i < distance_thresholds.size(); ++i) {
        if (adjusted_distance <= distance_thresholds[i]) {
            return i;
        }
    }
    
    return params_.max_lod;  // Beyond maximum distance
}

std::uint8_t BasicLODManager::ApplyHysteresis(std::uint8_t current_lod,
                                            std::uint8_t target_lod) const {
    
    if (std::abs(target_lod - current_lod) <= params_.hysteresis) {
        return current_lod;  // Apply hysteresis
    }
    
    return target_lod;
}

std::uint8_t BasicLODManager::CalculatePerformanceBasedLOD(std::uint8_t target_lod, 
                                                     float performance_impact) const {
    
    if (!params_.enable_adaptive) {
        return target_lod;  // No performance adaptation
    }
    
    // If performance impact is high, reduce LOD to maintain frame rate
    if (performance_impact > 1.5f) {  // More than 50% over budget
        if (target_lod > params_.min_lod) {
            return target_lod - 1;  // Reduce LOD by one level
        }
    }
    
    // If performance impact is low, can increase LOD
    if (performance_impact < 0.5f) {  // Well under budget
        if (target_lod < params_.max_lod) {
            return target_lod + 1;  // Increase LOD by one level
        }
    }
    
    return target_lod;
}

LODResult BasicLODManager::CalculateTriangleLOD(
    const GlobeTriangle& triangle,
    const glm::vec3& camera_position,
    const glm::mat4& /*view_matrix*/,
    const glm::mat4& /*projection_matrix*/,
    const glm::vec2& /*viewport_size*/) const {

    LODResult result;
    
    // Calculate screen-space error
    result.screen_error = triangle.screen_error;  // Pre-calculated in mesh
    
    // Calculate distance from camera to triangle center
    glm::vec3 triangle_center = (
        vertices_[triangle.vertices[0]].position +
        vertices_[triangle.vertices[1]].position +
        vertices_[triangle.vertices[2]].position
    ) / 3.0f;
    
    float distance = glm::length(camera_position - triangle_center);
    
    // Distance-based LOD
    std::uint8_t distance_lod = CalculateDistanceBasedLOD(distance);
    
    // Apply hysteresis to prevent LOD flickering
    result.lod_level = ApplyHysteresis(current_lod_level_, distance_lod);
    
    // Estimate GPU time
    result.gpu_time_ms = EstimateGPUTime(1, 3, result.lod_level);
    
    // Calculate performance impact
    if (params_.max_gpu_time > 0.0f) {
        result.performance_impact = result.gpu_time_ms / params_.max_gpu_time;
    } else {
        result.performance_impact = 0.0f;
    }
    
    // Check if LOD meets screen error requirements
    if (result.screen_error <= params_.max_screen_error) {
        result.valid = true;
    } else {
        result.valid = false;
        // LOD is too coarse, need higher detail
        result.lod_level = std::max(result.lod_level, static_cast<std::uint8_t>(1));
    }
    
    result.performance_impact = CalculatePerformanceBasedLOD(result.lod_level, 
                                                        result.performance_impact);
    
    return result;
}

LODResult BasicLODManager::CalculateTileLOD(const Tile& tile,
                                            const glm::vec3& /*camera_position*/,
                                            const glm::mat4& /*view_matrix*/,
                                            const glm::mat4& /*projection_matrix*/,
                                            const glm::vec2& /*viewport_size*/) const {
    LODResult result;
    
    // Use pre-calculated screen error from tile manager
    result.screen_error = tile.screen_error;
    
    // Calculate distance from camera to tile center
    // BoundingBox2D bounds = tile.geographic_bounds; // Unused for now
    // glm::vec2 tile_center = (bounds.min + bounds.max) * 0.5f; // Unused for now
    
    // Simple distance calculation (should be improved with proper geographic distance)
    float distance = tile.camera_distance;
    
    // Distance-based LOD
    std::uint8_t distance_lod = CalculateDistanceBasedLOD(distance);
    
    // Apply hysteresis
    result.lod_level = ApplyHysteresis(current_lod_level_, distance_lod);
    
    // Estimate GPU time (tiles are typically more efficient than individual triangles)
    std::size_t estimated_triangles = std::pow(2, static_cast<std::size_t>(result.lod_level)) * 2;  // Rough estimate
    result.gpu_time_ms = EstimateGPUTime(estimated_triangles, estimated_triangles * 3, result.lod_level);
    
    // Calculate performance impact
    if (params_.max_gpu_time > 0.0f) {
        result.performance_impact = result.gpu_time_ms / params_.max_gpu_time;
    } else {
        result.performance_impact = 0.0f;
    }
    
    // Check if LOD meets screen error requirements
    if (result.screen_error <= params_.max_screen_error) {
        result.valid = true;
    } else {
        result.valid = false;
        // LOD is too coarse, need higher detail
        result.lod_level = std::max(result.lod_level, static_cast<std::uint8_t>(1));
    }
    
    result.performance_impact = CalculatePerformanceBasedLOD(result.lod_level, 
                                                        result.performance_impact);
    
    return result;
}

void BasicLODManager::Update() {
    // Update frame statistics
    ++frame_count_;
    
    // Update running averages
    if (frame_count_ % 60 == 0) {  // Every 60 frames
        average_gpu_time_ = total_gpu_time_ / 60.0f;
        total_gpu_time_ = 0.0f;
    }
}

void BasicLODManager::UpdateStatistics(float gpu_time, std::uint8_t lod_level) {
    total_gpu_time_ += gpu_time;
    total_calculations_++;
    
    // Update current LOD with exponential smoothing
    float alpha = 0.1f;  // Smoothing factor
    current_lod_level_ = static_cast<std::uint8_t>(
        alpha * lod_level + (1.0f - alpha) * current_lod_level_
    );
}

std::pair<std::uint8_t, float> BasicLODManager::GetStatistics() const {
    float avg_gpu_time = frame_count_ > 0 ? 
                       average_gpu_time_ : 0.0f;
    return {current_lod_level_, avg_gpu_time};
}

void BasicLODManager::Reset() {
    total_calculations_ = 0;
    current_lod_level_ = 0;
    total_gpu_time_ = 0.0f;
    average_gpu_time_ = 0.0f;
    frame_count_ = 0;
    hysteresis_state_ = 0;
    
    spdlog::info("Reset LOD manager statistics");
}

LODParams BasicLODManager::GetParameters() const {
    return params_;
}

bool BasicLODManager::SetParameters(const LODParams& params) {
    params_ = params;
    
    spdlog::info("Updated LOD parameters: max_error={}, bias={}, adaptive={}",
                 params_.max_screen_error, params_.distance_bias, params_.enable_adaptive);
    
    return true;
}

// This needs to be updated if we want to use triangle vertices in LOD calculations
std::vector<GlobeVertex> BasicLODManager::vertices_;  // Placeholder

} // namespace earth_map
