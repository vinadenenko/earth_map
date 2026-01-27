// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/renderer/elevation_manager.h>
#include <earth_map/coordinates/coordinate_spaces.h>
#include <earth_map/constants.h>

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace earth_map {

/// Internal implementation of ElevationManager
class ElevationManagerImpl : public ElevationManager {
public:
    explicit ElevationManagerImpl(std::shared_ptr<ElevationProvider> provider)
        : elevation_provider_(std::move(provider)),
          config_() {
        if (!elevation_provider_) {
            throw std::invalid_argument("ElevationManager: elevation provider cannot be null");
        }
    }

    ~ElevationManagerImpl() override = default;

    bool Initialize(const ElevationConfig& config) override {
        config_ = config;
        spdlog::info("ElevationManager initialized: enabled={}, exaggeration={}x, generate_normals={}",
                     config_.enabled, config_.exaggeration_factor, config_.generate_normals);
        return true;
    }

    void ApplyElevationToMesh(std::vector<GlobeVertex>& vertices, double radius) override {
        // NOTE: 'radius' parameter is the rendering mesh radius (normalized, typically 1.0)
        // It is kept for API compatibility but not used in calculations.
        // Elevation conversion uses Earth's physical radius from constants.
        (void)radius;  // Explicitly mark as unused

        if (!config_.enabled) {
            return;
        }

        if (vertices.empty()) {
            spdlog::warn("ElevationManager::ApplyElevationToMesh: empty vertex array");
            return;
        }

        spdlog::debug("Applying elevation to {} vertices with exaggeration factor {}x",
                     vertices.size(), config_.exaggeration_factor);

        // Batch query elevations for all vertices
        std::vector<coordinates::Geographic> positions;
        positions.reserve(vertices.size());

        for (const auto& vertex : vertices) {
            // vertex.geographic = (longitude, latitude)
            positions.emplace_back(
                vertex.geographic.y,  // latitude
                vertex.geographic.x,  // longitude
                0.0                   // altitude (unused for elevation queries)
            );
        }

        // Query all elevations at once (more efficient than individual queries)
        auto elevations = elevation_provider_->GetElevations(positions);

        if (elevations.size() != vertices.size()) {
            spdlog::error("ElevationManager: elevation query returned {} results for {} vertices",
                         elevations.size(), vertices.size());
            return;
        }

        // Apply displacement to each vertex
        size_t applied_count = 0;
        float min_elevation = std::numeric_limits<float>::max();
        float max_elevation = std::numeric_limits<float>::lowest();
        float min_displacement = std::numeric_limits<float>::max();
        float max_displacement = std::numeric_limits<float>::lowest();

        for (size_t i = 0; i < vertices.size(); ++i) {
            if (!elevations[i].valid) {
                continue;
            }

            // Get elevation and apply clamping
            float elevation = elevations[i].elevation_meters;
            elevation = std::clamp(elevation, config_.min_elevation, config_.max_elevation);

            // Track elevation range
            min_elevation = std::min(min_elevation, elevation);
            max_elevation = std::max(max_elevation, elevation);

            // Apply vertical exaggeration
            float displacement = elevation * config_.exaggeration_factor;

            // Convert displacement from meters to normalized rendering units
            // NOTE: The 'radius' parameter is the rendering mesh radius (typically 1.0)
            // For unit conversion, we must use Earth's PHYSICAL radius in meters
            float normalized_displacement = static_cast<float>(
                displacement / constants::geodetic::EARTH_MEAN_RADIUS);

            // Track displacement range
            min_displacement = std::min(min_displacement, normalized_displacement);
            max_displacement = std::max(max_displacement, normalized_displacement);

            // Displace vertex along its normal vector
            // Normal is unit vector pointing outward from globe center
            vertices[i].position += vertices[i].normal * normalized_displacement;

            ++applied_count;
        }

        spdlog::info("Elevation Tag. Applied elevation to {}/{} vertices", applied_count, vertices.size());
        if (applied_count > 0) {
            spdlog::info("Elevation Tag. Elevation range: [{:.1f}m, {:.1f}m]", min_elevation, max_elevation);
            spdlog::info("Elevation Tag. Displacement range: [{:.6f}, {:.6f}] normalized units",
                        min_displacement, max_displacement);
            spdlog::info("Elevation Tag. Max displacement as % of radius: {:.2f}%", max_displacement * 100.0f);
        }
    }

    void GenerateNormals(std::vector<GlobeVertex>& vertices) override {
        if (!config_.enabled || !config_.generate_normals) {
            return;
        }

        if (vertices.empty()) {
            return;
        }

        spdlog::debug("Generating normals for {} vertices from elevation gradients",
                     vertices.size());

        // For each vertex, calculate normal from elevation gradients
        // This provides more accurate lighting for terrain features
        constexpr double SAMPLE_DELTA = 0.001;  // ~110 meters at equator

        for (auto& vertex : vertices) {
            const double lat = vertex.geographic.y;  // latitude
            const double lon = vertex.geographic.x;  // longitude

            // Sample elevation at center and neighboring points
            const auto center_query = elevation_provider_->GetElevation(lat, lon);
            const auto north_query = elevation_provider_->GetElevation(lat + SAMPLE_DELTA, lon);
            const auto east_query = elevation_provider_->GetElevation(lat, lon + SAMPLE_DELTA);
            const auto south_query = elevation_provider_->GetElevation(lat - SAMPLE_DELTA, lon);
            const auto west_query = elevation_provider_->GetElevation(lat, lon - SAMPLE_DELTA);

            // If any sample is invalid, keep original normal
            if (!center_query.valid || !north_query.valid || !east_query.valid ||
                !south_query.valid || !west_query.valid) {
                continue;
            }

            // Calculate elevation gradients (finite differences)
            const float dh_dlat = (north_query.elevation_meters - south_query.elevation_meters)
                                / static_cast<float>(2.0 * SAMPLE_DELTA);
            const float dh_dlon = (east_query.elevation_meters - west_query.elevation_meters)
                                / static_cast<float>(2.0 * SAMPLE_DELTA);

            // Convert gradients to tangent space vectors
            // Approximate tangent and bitangent in local coordinate frame
            const float lat_rad = static_cast<float>(glm::radians(lat));
            const float lon_rad = static_cast<float>(glm::radians(lon));

            // Calculate tangent vectors in world space
            // Tangent along latitude (north-south)
            const glm::vec3 tangent_lat(
                -std::sin(lon_rad) * dh_dlat,
                std::cos(lat_rad) * dh_dlat,
                std::cos(lon_rad) * dh_dlat
            );

            // Tangent along longitude (east-west)
            const glm::vec3 tangent_lon(
                -std::cos(lon_rad) * std::cos(lat_rad) * dh_dlon,
                -std::sin(lat_rad) * dh_dlon,
                -std::sin(lon_rad) * std::cos(lat_rad) * dh_dlon
            );

            // Normal is cross product of tangent vectors
            glm::vec3 normal = glm::cross(tangent_lon, tangent_lat);

            // Normalize and ensure it points outward
            const float length = glm::length(normal);
            if (length > 0.0f) {
                normal /= length;

                // Ensure normal points outward (dot product with position should be positive)
                if (glm::dot(normal, vertex.position) < 0.0f) {
                    normal = -normal;
                }

                vertex.normal = normal;
            }
        }

        spdlog::debug("Normal generation complete");
    }

    void SetConfiguration(const ElevationConfig& config) override {
        const bool was_enabled = config_.enabled;
        config_ = config;

        if (was_enabled != config_.enabled) {
            spdlog::info("Elevation rendering {}", config_.enabled ? "enabled" : "disabled");
        }
    }

    [[nodiscard]] ElevationConfig GetConfiguration() const override {
        return config_;
    }

    [[nodiscard]] ElevationProvider* GetElevationProvider() const override {
        return elevation_provider_.get();
    }

    void SetEnabled(bool enabled) override {
        config_.enabled = enabled;
        spdlog::info("Elevation rendering {}", enabled ? "enabled" : "disabled");
    }

    [[nodiscard]] bool IsEnabled() const override {
        return config_.enabled;
    }

private:
    std::shared_ptr<ElevationProvider> elevation_provider_;
    ElevationConfig config_;
};

// Factory function implementation
std::shared_ptr<ElevationManager> ElevationManager::Create(
    std::shared_ptr<ElevationProvider> provider) {
    return std::make_unique<ElevationManagerImpl>(std::move(provider));
}

} // namespace earth_map
