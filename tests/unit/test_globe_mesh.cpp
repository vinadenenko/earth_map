/**
 * @file test_globe_mesh.cpp
 * @brief Unit tests for globe mesh generation
 */

#include <gtest/gtest.h>
#include <earth_map/renderer/globe_mesh.h>
#include <glm/glm.hpp>
#include <cmath>

using namespace earth_map;

class GlobeMeshTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Default parameters for testing
        params_.radius = 6378137.0;  // Earth radius in meters
        params_.max_subdivision_level = 2;
        params_.enable_adaptive = true;
        params_.quality = MeshQuality::MEDIUM;
    }
    
    GlobeMeshParams params_;
};

TEST_F(GlobeMeshTest, CreateGlobeMesh) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->GetParameters().radius, params_.radius);
}

TEST_F(GlobeMeshTest, GenerateBaseMesh) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    EXPECT_TRUE(mesh->Generate());
    
    // Check that we have the expected base icosahedron
    auto [vertices, triangles] = mesh->GetStatistics();
    EXPECT_EQ(vertices, 12);  // Base icosahedron has 12 vertices
    EXPECT_EQ(triangles, 20);  // Base icosahedron has 20 faces
}

TEST_F(GlobeMeshTest, SubdivisionLevels) {
    params_.max_subdivision_level = 1;
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    EXPECT_TRUE(mesh->Generate());
    
    auto [vertices, triangles] = mesh->GetStatistics();
    EXPECT_GT(vertices, 12);  // Should have more vertices after subdivision
    EXPECT_EQ(triangles, 80);  // Each triangle divides into 4 = 20*4
}

TEST_F(GlobeMeshTest, MeshValidation) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    EXPECT_TRUE(mesh->Generate());
    EXPECT_TRUE(mesh->Validate());
}

TEST_F(GlobeMeshTest, GeographicCoordinates) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    EXPECT_TRUE(mesh->Generate());
    
    const auto& vertices = mesh->GetVertices();
    ASSERT_FALSE(vertices.empty());
    
    // Check that geographic coordinates are reasonable
    for (const auto& vertex : vertices) {
        EXPECT_GE(vertex.geographic.x, -180.0f);
        EXPECT_LE(vertex.geographic.x, 180.0f);
        EXPECT_GE(vertex.geographic.y, -90.0f);
        EXPECT_LE(vertex.geographic.y, 90.0f);
        
        // Check UV coordinates are in valid range
        EXPECT_GE(vertex.texcoord.x, 0.0f);
        EXPECT_LE(vertex.texcoord.x, 1.0f);
        EXPECT_GE(vertex.texcoord.y, 0.0f);
        EXPECT_LE(vertex.texcoord.y, 1.0f);
    }
}

TEST_F(GlobeMeshTest, VertexNormals) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    EXPECT_TRUE(mesh->Generate());
    
    const auto& vertices = mesh->GetVertices();
    
    // For a sphere, normals should point outward from center
    for (const auto& vertex : vertices) {
        float normal_length = glm::length(vertex.normal);
        EXPECT_NEAR(normal_length, 1.0f, 1e-6f) << "Normal should be unit length";
        
        // Normal should point in same direction as position (for sphere)
        glm::vec3 expected_normal = glm::normalize(vertex.position);
        EXPECT_NEAR(vertex.normal.x, expected_normal.x, 1e-6f);
        EXPECT_NEAR(vertex.normal.y, expected_normal.y, 1e-6f);
        EXPECT_NEAR(vertex.normal.z, expected_normal.z, 1e-6f);
    }
}

TEST_F(GlobeMeshTest, MeshQualitySettings) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    // Test different quality settings
    mesh->SetQuality(MeshQuality::LOW);
    EXPECT_EQ(mesh->GetQuality(), MeshQuality::LOW);
    EXPECT_EQ(mesh->GetParameters().max_subdivision_level, 3);
    
    mesh->SetQuality(MeshQuality::HIGH);
    EXPECT_EQ(mesh->GetQuality(), MeshQuality::HIGH);
    EXPECT_EQ(mesh->GetParameters().max_subdivision_level, 7);
    
    mesh->SetQuality(MeshQuality::ULTRA);
    EXPECT_EQ(mesh->GetQuality(), MeshQuality::ULTRA);
    EXPECT_EQ(mesh->GetParameters().max_subdivision_level, 8);
}

TEST_F(GlobeMeshTest, BoundingBoxCalculation) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    EXPECT_TRUE(mesh->Generate());
    
    auto bounds = mesh->CalculateBounds();
    EXPECT_TRUE(bounds.IsValid());
    
    // Should cover the entire globe
    EXPECT_LE(bounds.min.x, -180.0f);
    EXPECT_GE(bounds.max.x, 180.0f);
    EXPECT_LE(bounds.min.y, -90.0f);
    EXPECT_GE(bounds.max.y, 90.0f);
}

TEST_F(GlobeMeshTest, VertexSearch) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    EXPECT_TRUE(mesh->Generate());
    
    // Search for vertices near a specific location
    auto nearby_vertices = mesh->FindVerticesAtLocation(0.0, 0.0, 1000.0);
    EXPECT_FALSE(nearby_vertices.empty());
    
    // Search with small radius should return fewer results
    auto very_nearby = mesh->FindVerticesAtLocation(0.0, 0.0, 100.0);
    EXPECT_LE(very_nearby.size(), nearby_vertices.size());
}

TEST_F(GlobeMeshTest, TriangleBoundsQuery) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    EXPECT_TRUE(mesh->Generate());
    
    // Create bounds covering a small area
    BoundingBox2D search_bounds;
    search_bounds.min = glm::vec2(-10.0f, -10.0f);
    search_bounds.max = glm::vec2(10.0f, 10.0f);
    
    auto triangles_in_bounds = mesh->GetTrianglesInBounds(search_bounds);
    EXPECT_FALSE(triangles_in_bounds.empty());
    
    // Most triangles should be outside this small bounds
    const auto& all_triangles = mesh->GetTriangles();
    EXPECT_LT(triangles_in_bounds.size(), all_triangles.size());
}

TEST_F(GlobeMeshTest, VertexIndexGeneration) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    EXPECT_TRUE(mesh->Generate());
    
    auto vertex_indices = mesh->GetVertexIndices();
    const auto& triangles = mesh->GetTriangles();
    
    // Should have 3 indices per triangle
    EXPECT_EQ(vertex_indices.size(), triangles.size() * 3);
    
    // All indices should be valid
    const auto& vertices = mesh->GetVertices();
    for (uint32_t index : vertex_indices) {
        EXPECT_LT(index, vertices.size());
    }
}

class GlobeMeshPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        params_.radius = 6378137.0;
        params_.max_subdivision_level = 6;  // Higher level for performance testing
        params_.enable_adaptive = true;
        params_.quality = MeshQuality::HIGH;
    }
    
    GlobeMeshParams params_;
};

TEST_F(GlobeMeshPerformanceTest, GenerationPerformance) {
    auto mesh = GlobeMesh::Create(params_);
    ASSERT_NE(mesh, nullptr);
    
    auto start = std::chrono::high_resolution_clock::now();
    EXPECT_TRUE(mesh->Generate());
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should complete within reasonable time (adjust as needed)
    EXPECT_LT(duration.count(), 1000) << "Mesh generation took too long: " 
                                         << duration.count() << "ms";
    
    auto [vertices, triangles] = mesh->GetStatistics();
    EXPECT_GT(vertices, 10000) << "Should have substantial vertex count for high quality";
    EXPECT_GT(triangles, 20000) << "Should have substantial triangle count for high quality";
}