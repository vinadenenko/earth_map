// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#pragma once

#include <earth_map/data/elevation_provider.h>
#include <earth_map/renderer/globe_mesh.h>

#include <memory>

namespace earth_map {

/// Configuration for elevation rendering
struct ElevationConfig {
    /// Enable elevation rendering
    bool enabled = false;

    /// Vertical exaggeration factor for visual clarity
    /// 1.0 = true scale, 2.0 = double height, etc.
    float exaggeration_factor = 2.0f;

    /// Generate surface normals from elevation gradients for realistic lighting
    bool generate_normals = true;

    /// Enable level-of-detail management for elevation data
    bool use_lod = true;

    /// Maximum SRTM resolution to use
    SRTMResolution max_resolution = SRTMResolution::SRTM3;

    /// Minimum elevation value to clamp to (meters)
    float min_elevation = -500.0f;

    /// Maximum elevation value to clamp to (meters)
    float max_elevation = 9000.0f;
};

/// Manages elevation data application to globe mesh vertices
/// Applies SRTM elevation data to globe mesh for 3D terrain visualization
class ElevationManager {
public:
    virtual ~ElevationManager() = default;

    /// Initialize elevation manager with configuration
    /// @param config Elevation rendering configuration
    /// @return True on success, false on failure
    virtual bool Initialize(const ElevationConfig& config) = 0;

    /// Apply elevation data to globe mesh vertices (CPU-side displacement)
    /// Displaces each vertex along its normal vector based on elevation data
    /// @param vertices Mesh vertices to modify in-place
    /// @param radius Base globe radius in meters
    virtual void ApplyElevationToMesh(std::vector<GlobeVertex>& vertices,
                                      double radius) = 0;

    /// Generate surface normals from elevation gradients
    /// Calculates realistic normals for proper terrain lighting
    /// @param vertices Mesh vertices with elevation already applied
    virtual void GenerateNormals(std::vector<GlobeVertex>& vertices) = 0;

    /// Set elevation configuration
    /// @param config New configuration
    virtual void SetConfiguration(const ElevationConfig& config) = 0;

    /// Get current elevation configuration
    /// @return Current configuration
    [[nodiscard]] virtual ElevationConfig GetConfiguration() const = 0;

    /// Get elevation provider for direct queries
    /// @return Pointer to elevation provider (non-owning)
    [[nodiscard]] virtual ElevationProvider* GetElevationProvider() const = 0;

    /// Enable or disable elevation rendering
    /// @param enabled True to enable, false to disable
    virtual void SetEnabled(bool enabled) = 0;

    /// Check if elevation rendering is enabled
    /// @return True if enabled
    [[nodiscard]] virtual bool IsEnabled() const = 0;

    /// Create elevation manager instance
    /// @param provider Elevation data provider
    /// @return Shared pointer to elevation manager
    [[nodiscard]] static std::shared_ptr<ElevationManager> Create(
        std::shared_ptr<ElevationProvider> provider);
};

} // namespace earth_map
