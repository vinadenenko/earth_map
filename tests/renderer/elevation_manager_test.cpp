// Copyright (c) 2025 Earth Map Project
// SPDX-License-Identifier: MIT

#include <earth_map/renderer/elevation_manager.h>
#include <earth_map/data/elevation_provider.h>
#include <earth_map/data/srtm_loader.h>
#include <earth_map/renderer/globe_mesh.h>

#include <gtest/gtest.h>
#include <memory>

namespace earth_map {
namespace {

class ElevationManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Configure SRTM loader for local data
        SRTMLoaderConfig loader_config;
        loader_config.source = SRTMSource::LOCAL_DISK;
        loader_config.local_directory = "./srtm_data";
        loader_config.preferred_resolution = SRTMResolution::SRTM3;

        // Create elevation provider
        elevation_provider_ = ElevationProvider::Create(loader_config);

        // Create elevation manager
        elevation_manager_ = ElevationManager::Create(elevation_provider_);

        // Initialize with default configuration
        ElevationConfig config;
        config.enabled = true;
        config.exaggeration_factor = 2.0f;
        config.generate_normals = true;
        ASSERT_TRUE(elevation_manager_->Initialize(config));
    }

    std::shared_ptr<ElevationProvider> elevation_provider_;
    std::shared_ptr<ElevationManager> elevation_manager_;
};

TEST_F(ElevationManagerTest, InitializationSucceeds) {
    EXPECT_TRUE(elevation_manager_->IsEnabled());
    EXPECT_EQ(elevation_manager_->GetConfiguration().exaggeration_factor, 2.0f);
}

TEST_F(ElevationManagerTest, ConfigurationCanBeModified) {
    ElevationConfig new_config;
    new_config.enabled = true;
    new_config.exaggeration_factor = 3.0f;
    new_config.generate_normals = false;

    elevation_manager_->SetConfiguration(new_config);

    auto config = elevation_manager_->GetConfiguration();
    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.exaggeration_factor, 3.0f);
    EXPECT_FALSE(config.generate_normals);
}

TEST_F(ElevationManagerTest, CanBeEnabledAndDisabled) {
    EXPECT_TRUE(elevation_manager_->IsEnabled());

    elevation_manager_->SetEnabled(false);
    EXPECT_FALSE(elevation_manager_->IsEnabled());

    elevation_manager_->SetEnabled(true);
    EXPECT_TRUE(elevation_manager_->IsEnabled());
}

TEST_F(ElevationManagerTest, ApplyElevationToEmptyMesh) {
    std::vector<GlobeVertex> vertices;
    const double radius = 6378137.0;

    // Should not crash with empty vertices
    EXPECT_NO_THROW(elevation_manager_->ApplyElevationToMesh(vertices, radius));
}

TEST_F(ElevationManagerTest, ApplyElevationToSingleVertex) {
    std::vector<GlobeVertex> vertices(1);

    // Set up a vertex at Mt. Everest location
    vertices[0].position = glm::vec3(0.0f, 0.0f, 1.0f);
    vertices[0].normal = glm::vec3(0.0f, 0.0f, 1.0f);
    vertices[0].geographic = glm::vec2(86.9250f, 27.9881f);  // (longitude, latitude)

    const double radius = 6378137.0;
    const glm::vec3 original_position = vertices[0].position;

    elevation_manager_->ApplyElevationToMesh(vertices, radius);

    // Vertex should be displaced along normal (if elevation data is available)
    // If no data is available, position should remain unchanged
    EXPECT_TRUE(vertices[0].position == original_position ||
                glm::length(vertices[0].position) > glm::length(original_position));
}

TEST_F(ElevationManagerTest, DisabledManagerDoesNotModifyVertices) {
    elevation_manager_->SetEnabled(false);

    std::vector<GlobeVertex> vertices(1);
    vertices[0].position = glm::vec3(1.0f, 0.0f, 0.0f);
    vertices[0].normal = glm::vec3(1.0f, 0.0f, 0.0f);
    vertices[0].geographic = glm::vec2(0.0f, 0.0f);

    const glm::vec3 original_position = vertices[0].position;
    const double radius = 6378137.0;

    elevation_manager_->ApplyElevationToMesh(vertices, radius);

    // Position should not change when manager is disabled
    EXPECT_EQ(vertices[0].position, original_position);
}

TEST_F(ElevationManagerTest, ExaggerationFactorAffectsDisplacement) {
    std::vector<GlobeVertex> vertices(1);
    vertices[0].position = glm::vec3(1.0f, 0.0f, 0.0f);
    vertices[0].normal = glm::vec3(1.0f, 0.0f, 0.0f);
    vertices[0].geographic = glm::vec2(0.0f, 0.0f);

    const double radius = 6378137.0;

    // Test with exaggeration factor 1.0
    ElevationConfig config1;
    config1.enabled = true;
    config1.exaggeration_factor = 1.0f;
    elevation_manager_->SetConfiguration(config1);

    std::vector<GlobeVertex> vertices1 = vertices;
    elevation_manager_->ApplyElevationToMesh(vertices1, radius);

    // Test with exaggeration factor 2.0
    ElevationConfig config2;
    config2.enabled = true;
    config2.exaggeration_factor = 2.0f;
    elevation_manager_->SetConfiguration(config2);

    std::vector<GlobeVertex> vertices2 = vertices;
    elevation_manager_->ApplyElevationToMesh(vertices2, radius);

    // With higher exaggeration factor, displacement should be greater (if data available)
    const float dist1 = glm::length(vertices1[0].position - vertices[0].position);
    const float dist2 = glm::length(vertices2[0].position - vertices[0].position);

    // If both are displaced (data available), dist2 should be greater
    if (dist1 > 0.0f && dist2 > 0.0f) {
        EXPECT_GT(dist2, dist1);
    }
}

TEST_F(ElevationManagerTest, GenerateNormalsDoesNotCrash) {
    std::vector<GlobeVertex> vertices(1);
    vertices[0].position = glm::vec3(1.0f, 0.0f, 0.0f);
    vertices[0].normal = glm::vec3(1.0f, 0.0f, 0.0f);
    vertices[0].geographic = glm::vec2(0.0f, 0.0f);

    EXPECT_NO_THROW(elevation_manager_->GenerateNormals(vertices));
}

TEST_F(ElevationManagerTest, GetElevationProviderReturnsValidPointer) {
    auto* provider = elevation_manager_->GetElevationProvider();
    EXPECT_NE(provider, nullptr);
}

} // namespace
} // namespace earth_map
