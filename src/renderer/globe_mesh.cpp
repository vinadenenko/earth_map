/**
 * @file globe_mesh.cpp
 * @brief Globe mesh generation with icosahedron-based tessellation implementation
 */

#include <earth_map/renderer/globe_mesh.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace earth_map {

// Factory function
std::unique_ptr<GlobeMesh> GlobeMesh::Create(const GlobeMeshParams& params) {
    return std::make_unique<IcosahedronGlobeMesh>(params);
}

IcosahedronGlobeMesh::IcosahedronGlobeMesh(const GlobeMeshParams& params) 
    : params_(params) {
}

bool IcosahedronGlobeMesh::Generate() {
    spdlog::info("Generating globe mesh with radius {} meters", params_.radius);
    
    try {
        vertices_.clear();
        triangles_.clear();
        vertex_indices_.clear();
        
        // Generate base icosahedron
        GenerateIcosahedron();
        
        // Subdivide if adaptive tessellation is enabled
        if (params_.enable_adaptive) {
            spdlog::info("Performing adaptive subdivision up to level {}", params_.max_subdivision_level);
            // For now, do uniform subdivision to max level
            // TODO: Implement camera-based adaptive subdivision
            SubdivideToLevel(params_.max_subdivision_level);
        }
        
        // Generate vertex indices for rendering
        GenerateVertexIndices();
        
        // Optimize mesh for GPU rendering
        if (!Optimize()) {
            spdlog::warn("Mesh optimization failed, using unoptimized mesh");
        }
        
        // Validate mesh
        if (!Validate()) {
            spdlog::error("Generated mesh failed validation");
            return false;
        }
        
        spdlog::info("Globe mesh generated: {} vertices, {} triangles", 
                   vertices_.size(), triangles_.size());
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Exception in globe mesh generation: {}", e.what());
        return false;
    }
}

void IcosahedronGlobeMesh::GenerateIcosahedron() {
    // Golden ratio for icosahedron construction
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    const double a = 1.0;
    const double b = 1.0 / phi;
    
    // Normalize to unit sphere, then scale to radius
    const auto normalize_and_scale = [&](double x, double y, double z) -> glm::vec3 {
        const double length = std::sqrt(x*x + y*y + z*z);
        return glm::vec3(
            static_cast<float>((x / length) * params_.radius),
            static_cast<float>((y / length) * params_.radius),
            static_cast<float>((z / length) * params_.radius)
        );
    };
    
    // Create 12 vertices of icosahedron
    std::vector<glm::vec3> base_vertices = {
        normalize_and_scale(0,  b, -a),   // 0
        normalize_and_scale( b,  a,  0),    // 1
        normalize_and_scale(-b,  a,  0),    // 2
        normalize_and_scale(0,  a,  b),     // 3
        normalize_and_scale(0,  a, -b),     // 4
        normalize_and_scale( b, -a,  0),    // 5
        normalize_and_scale(-b, -a,  0),    // 6
        normalize_and_scale(0, -a,  b),     // 7
        normalize_and_scale(0, -a, -b),     // 8
        normalize_and_scale( a,  0,  b),    // 9
        normalize_and_scale(-a,  0,  b),    // 10
        normalize_and_scale(-a,  0, -b),    // 11
        normalize_and_scale( a,  0, -b)     // 12
    };
    
    // Create GlobeVertex objects from positions
    vertices_.reserve(base_vertices.size());
    for (const auto& pos : base_vertices) {
        GlobeVertex vertex;
        vertex.position = pos;
        vertex.normal = glm::normalize(pos);  // For sphere, normal = normalized position
        vertex.geographic = PositionToGeographic(pos);
        vertex.texcoord = GeographicToUV(vertex.geographic);
        vertex.lod_level = 0;
        vertex.edge_flags = 0;
        vertices_.push_back(vertex);
    }
    
    // Define 20 faces of icosahedron (using 0-based indexing)
    std::vector<std::array<int, 3>> faces = {
        {0, 1, 4}, {0, 4, 9}, {4, 5, 9}, {5, 8, 9}, {1, 2, 5},
        {2, 6, 5}, {6, 7, 5}, {7, 8, 5}, {3, 2, 6}, {3, 6, 7},
        {3, 7, 10}, {10, 11, 7}, {11, 8, 7}, {11, 12, 8}, {12, 9, 8},
        {12, 1, 9}, {1, 0, 12}, {0, 12, 11}, {0, 11, 10}, {0, 10, 3}
    };
    
    // Create GlobeTriangle objects
    triangles_.reserve(faces.size());
    for (const auto& face : faces) {
        GlobeTriangle triangle;
        triangle.vertices[0] = face[0];
        triangle.vertices[1] = face[1];
        triangle.vertices[2] = face[2];
        triangle.lod_level = 0;
        triangle.screen_error = 0.0f;
        triangle.visible = true;
        triangle.bounds = CalculateTriangleBounds(triangle);
        triangles_.push_back(triangle);
    }
    
    spdlog::debug("Generated base icosahedron: {} vertices, {} triangles", 
                  vertices_.size(), triangles_.size());
}

void IcosahedronGlobeMesh::SubdivideToLevel(std::uint8_t target_level) {
    if (target_level == 0) return;
    
    for (std::uint8_t level = 1; level <= target_level; ++level) {
        spdlog::debug("Subdividing to level {}", level);
        std::vector<GlobeTriangle> new_triangles;
        new_triangles.reserve(triangles_.size() * 4);
        
        for (const auto& triangle : triangles_) {
            if (triangle.lod_level >= level) {
                // Triangle already at this level, keep as is
                new_triangles.push_back(triangle);
                continue;
            }
            
            // Get vertex indices
            std::uint32_t v0 = triangle.vertices[0];
            std::uint32_t v1 = triangle.vertices[1];
            std::uint32_t v2 = triangle.vertices[2];
            
            // Create midpoint vertices
            std::uint32_t v01 = CreateMidpointVertex(v0, v1);
            std::uint32_t v12 = CreateMidpointVertex(v1, v2);
            std::uint32_t v20 = CreateMidpointVertex(v2, v0);
            
            // Create 4 new triangles
            GlobeTriangle t1, t2, t3, t4;
            
            // Triangle 1 (top)
            t1.vertices[0] = v0;
            t1.vertices[1] = v01;
            t1.vertices[2] = v20;
            t1.lod_level = level;
            
            // Triangle 2 (right)
            t2.vertices[0] = v01;
            t2.vertices[1] = v1;
            t2.vertices[2] = v12;
            t2.lod_level = level;
            
            // Triangle 3 (bottom)
            t3.vertices[0] = v20;
            t3.vertices[1] = v12;
            t3.vertices[2] = v2;
            t3.lod_level = level;
            
            // Triangle 4 (center)
            t4.vertices[0] = v01;
            t4.vertices[1] = v12;
            t4.vertices[2] = v20;
            t4.lod_level = level;
            
            // Calculate bounds for new triangles
            t1.bounds = CalculateTriangleBounds(t1);
            t2.bounds = CalculateTriangleBounds(t2);
            t3.bounds = CalculateTriangleBounds(t3);
            t4.bounds = CalculateTriangleBounds(t4);
            
            new_triangles.push_back(t1);
            new_triangles.push_back(t2);
            new_triangles.push_back(t3);
            new_triangles.push_back(t4);
        }
        
        triangles_ = std::move(new_triangles);
    }
    
    // Update LOD levels for vertices (take max of adjacent triangles)
    for (auto& vertex : vertices_) {
        vertex.lod_level = 0;
    }
    
    for (const auto& triangle : triangles_) {
        for (int i = 0; i < 3; ++i) {
            vertices_[triangle.vertices[i]].lod_level = 
                std::max(vertices_[triangle.vertices[i]].lod_level, triangle.lod_level);
        }
    }
}

std::size_t IcosahedronGlobeMesh::CreateMidpointVertex(std::size_t v1, std::size_t v2) {
    // Create a key for the edge to avoid duplicate vertices
    std::uint64_t edge_key = (static_cast<std::uint64_t>(std::min(v1, v2)) << 32) | 
                             static_cast<std::uint64_t>(std::max(v1, v2));
    
    auto it = midpoint_cache_.find(edge_key);
    if (it != midpoint_cache_.end()) {
        return it->second;
    }
    
    // Calculate midpoint position
    const glm::vec3& pos1 = vertices_[v1].position;
    const glm::vec3& pos2 = vertices_[v2].position;
    glm::vec3 midpoint = (pos1 + pos2) * 0.5f;
    
    // Normalize to sphere surface and scale to radius
    midpoint = glm::normalize(midpoint) * static_cast<float>(params_.radius);
    
    // Create new vertex
    GlobeVertex vertex;
    vertex.position = midpoint;
    vertex.normal = glm::normalize(midpoint);
    vertex.geographic = PositionToGeographic(midpoint);
    vertex.texcoord = GeographicToUV(vertex.geographic);
    vertex.lod_level = std::max(vertices_[v1].lod_level, vertices_[v2].lod_level) + 1;
    vertex.edge_flags = 0;
    
    std::size_t new_vertex_index = vertices_.size();
    vertices_.push_back(vertex);
    
    // Cache the midpoint
    midpoint_cache_[edge_key] = new_vertex_index;
    
    return new_vertex_index;
}

glm::vec2 IcosahedronGlobeMesh::PositionToGeographic(const glm::vec3& position) const {
    // Convert Cartesian position to geographic coordinates
    const double x = position.x;
    const double y = position.y;
    const double z = position.z;
    
    const double radius = std::sqrt(x*x + y*y + z*z);
    const double lat_rad = std::asin(y / radius);
    const double lon_rad = std::atan2(x, z);
    
    return glm::vec2(
        static_cast<float>(lon_rad * 180.0 / M_PI),
        static_cast<float>(lat_rad * 180.0 / M_PI)
    );
}

glm::vec2 IcosahedronGlobeMesh::GeographicToUV(const glm::vec2& geographic) const {
    // Convert geographic coordinates to UV coordinates
    float u = static_cast<float>((geographic.x + 180.0) / 360.0);
    float v = static_cast<float>((geographic.y + 90.0) / 180.0);
    
    // Clamp to valid range
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    
    return glm::vec2(u, v);
}

void IcosahedronGlobeMesh::GenerateVertexIndices() {
    vertex_indices_.clear();
    vertex_indices_.reserve(triangles_.size() * 3);
    
    for (const auto& triangle : triangles_) {
        for (int i = 0; i < 3; ++i) {
            vertex_indices_.push_back(triangle.vertices[i]);
        }
    }
}

BoundingBox2D IcosahedronGlobeMesh::CalculateTriangleBounds(const GlobeTriangle& triangle) const {
    BoundingBox2D bounds;
    
    for (int i = 0; i < 3; ++i) {
        const glm::vec2& geo = vertices_[triangle.vertices[i]].geographic;
        if (i == 0) {
            bounds.min = geo;
            bounds.max = geo;
        } else {
            bounds.min.x = std::min(bounds.min.x, geo.x);
            bounds.min.y = std::min(bounds.min.y, geo.y);
            bounds.max.x = std::max(bounds.max.x, geo.x);
            bounds.max.y = std::max(bounds.max.y, geo.y);
        }
    }
    
    return bounds;
}

bool IcosahedronGlobeMesh::UpdateLOD(const glm::vec3& camera_position,
                                   const glm::mat4& view_matrix,
                                   const glm::mat4& projection_matrix,
                                   const glm::vec2& viewport_size) {
    // TODO: Implement camera-based adaptive LOD
    // For now, return true without changes
    (void)camera_position;
    (void)view_matrix;
    (void)projection_matrix;
    (void)viewport_size;
    return true;
}

const std::vector<GlobeVertex>& IcosahedronGlobeMesh::GetVertices() const {
    return vertices_;
}

const std::vector<GlobeTriangle>& IcosahedronGlobeMesh::GetTriangles() const {
    return triangles_;
}

std::vector<std::uint32_t> IcosahedronGlobeMesh::GetVertexIndices() const {
    return vertex_indices_;
}

GlobeMeshParams IcosahedronGlobeMesh::GetParameters() const {
    return params_;
}

void IcosahedronGlobeMesh::SetQuality(MeshQuality quality) {
    params_.quality = quality;
    
    // Adjust parameters based on quality
    switch (quality) {
        case MeshQuality::LOW:
            params_.max_subdivision_level = 3;
            params_.max_screen_error = 4.0f;
            break;
        case MeshQuality::MEDIUM:
            params_.max_subdivision_level = 5;
            params_.max_screen_error = 2.0f;
            break;
        case MeshQuality::HIGH:
            params_.max_subdivision_level = 7;
            params_.max_screen_error = 1.0f;
            break;
        case MeshQuality::ULTRA:
            params_.max_subdivision_level = 8;
            params_.max_screen_error = 0.5f;
            break;
    }
}

MeshQuality IcosahedronGlobeMesh::GetQuality() const {
    return params_.quality;
}

std::pair<std::size_t, std::size_t> IcosahedronGlobeMesh::GetStatistics() const {
    return {vertices_.size(), triangles_.size()};
}

bool IcosahedronGlobeMesh::Validate() const {
    // Check basic mesh integrity
    if (vertices_.empty() || triangles_.empty()) {
        spdlog::error("Mesh validation failed: empty vertices or triangles");
        return false;
    }
    
    // Check triangle indices
    for (const auto& triangle : triangles_) {
        for (int i = 0; i < 3; ++i) {
            if (triangle.vertices[i] >= vertices_.size()) {
                spdlog::error("Mesh validation failed: invalid vertex index {}", triangle.vertices[i]);
                return false;
            }
        }
    }
    
    // Check vertex data
    for (const auto& vertex : vertices_) {
        if (glm::length(vertex.position) < params_.radius * 0.9f || 
            glm::length(vertex.position) > params_.radius * 1.1f) {
            spdlog::error("Mesh validation failed: vertex position out of range");
            return false;
        }
        
        if (glm::length(vertex.normal) < 0.9f || glm::length(vertex.normal) > 1.1f) {
            spdlog::error("Mesh validation failed: vertex normal not normalized");
            return false;
        }
    }
    
    spdlog::debug("Mesh validation passed");
    return true;
}

bool IcosahedronGlobeMesh::Optimize() {
    // TODO: Implement vertex cache optimization
    // For now, just generate vertex indices
    GenerateVertexIndices();
    return true;
}

BoundingBox2D IcosahedronGlobeMesh::CalculateBounds() const {
    if (vertices_.empty()) {
        return BoundingBox2D();
    }
    
    BoundingBox2D bounds;
    bounds.min = vertices_[0].geographic;
    bounds.max = vertices_[0].geographic;
    
    for (const auto& vertex : vertices_) {
        bounds.min.x = std::min(bounds.min.x, vertex.geographic.x);
        bounds.min.y = std::min(bounds.min.y, vertex.geographic.y);
        bounds.max.x = std::max(bounds.max.x, vertex.geographic.x);
        bounds.max.y = std::max(bounds.max.y, vertex.geographic.y);
    }
    
    return bounds;
}

std::vector<std::size_t> IcosahedronGlobeMesh::FindVerticesAtLocation(
    double longitude, double latitude, double radius) const {
    std::vector<std::size_t> nearby_vertices;
    
    glm::vec2 target_geo(static_cast<float>(longitude), static_cast<float>(latitude));
    
    for (std::size_t i = 0; i < vertices_.size(); ++i) {
        const glm::vec2& vertex_geo = vertices_[i].geographic;
        
        // Simple distance check (approximate)
        float dx = vertex_geo.x - target_geo.x;
        float dy = vertex_geo.y - target_geo.y;
        float distance_squared = dx*dx + dy*dy;
        
        if (distance_squared <= radius*radius) {
            nearby_vertices.push_back(i);
        }
    }
    
    return nearby_vertices;
}

std::vector<std::size_t> IcosahedronGlobeMesh::GetTrianglesInBounds(
    const BoundingBox2D& bounds) const {
    std::vector<std::size_t> triangles_in_bounds;
    
    for (std::size_t i = 0; i < triangles_.size(); ++i) {
        if (bounds.Intersects(triangles_[i].bounds)) {
            triangles_in_bounds.push_back(i);
        }
    }
    
    return triangles_in_bounds;
}

} // namespace earth_map