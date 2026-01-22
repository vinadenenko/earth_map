/**
 * @file globe_mesh.cpp
 * @brief Globe mesh generation with icosahedron-based tessellation implementation
 */

#include <earth_map/renderer/globe_mesh.h>
#include <earth_map/math/bounding_box.h>
#include <earth_map/coordinates/coordinate_mapper.h>
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

        // CRITICAL FIX: Always subdivide to the specified level, regardless of enable_adaptive
        // The enable_adaptive flag controls RUNTIME LOD changes, not initial subdivision
        if (params_.max_subdivision_level > 0) {
            spdlog::info("Subdividing globe mesh to level {}", params_.max_subdivision_level);
            SubdivideToLevel(params_.max_subdivision_level);
            spdlog::info("Subdivision complete: {} vertices, {} triangles",
                        vertices_.size(), triangles_.size());
        } else {
            spdlog::info("Subdivision skipped: max_subdivision_level = 0 (base icosahedron only)");
        }

        // Log whether runtime adaptive LOD is enabled
        if (params_.enable_adaptive) {
            spdlog::info("Runtime adaptive LOD enabled - mesh will adapt during camera movement");
        } else {
            spdlog::info("Runtime adaptive LOD disabled - using static mesh at level {}",
                        params_.max_subdivision_level);
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
    // Standard icosahedron using golden ratio
    // Proper 12-vertex topology with consistent CCW winding
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;  // Golden ratio: ~1.618

    // Normalize to unit sphere, then scale to radius
    const auto normalize_and_scale = [&](double x, double y, double z) -> glm::vec3 {
        const double length = std::sqrt(x*x + y*y + z*z);
        return glm::vec3(
            static_cast<float>((x / length) * params_.radius),
            static_cast<float>((y / length) * params_.radius),
            static_cast<float>((z / length) * params_.radius)
        );
    };

    // Create 12 vertices of standard icosahedron
    // Using rectangles in perpendicular planes construction
    // Vertices form 3 groups:
    // - 4 vertices in Y=0 plane (equator)
    // - 4 vertices in X=0 plane (prime meridian)
    // - 4 vertices in Z=0 plane (90°E/W meridian)
    std::vector<glm::vec3> base_vertices = {
        // Top pentagon (5 vertices around north pole)
        normalize_and_scale( 0,    1,   phi),  // 0
        normalize_and_scale( phi,  1,   0  ),  // 1
        normalize_and_scale( 0,    1,  -phi),  // 2
        normalize_and_scale(-phi,  1,   0  ),  // 3
        normalize_and_scale( 0,    1,   phi),  // 4 (duplicate of 0 for now)

        // Bottom pentagon (5 vertices around south pole)
        normalize_and_scale( 0,   -1,   phi),  // 5
        normalize_and_scale( phi, -1,   0  ),  // 6
        normalize_and_scale( 0,   -1,  -phi),  // 7
        normalize_and_scale(-phi, -1,   0  ),  // 8
        normalize_and_scale( 0,   -1,   phi),  // 9 (duplicate of 5)

        // Equatorial band (2 more to make 12)
        normalize_and_scale( phi,  0,   phi),  // 10
        normalize_and_scale(-phi,  0,  -phi)   // 11
    };

    // CORRECT STANDARD ICOSAHEDRON - 12 vertices:
    // Using canonical icosahedron vertex positions
    base_vertices.clear();
    base_vertices = {
        // Vertices arranged as 3 perpendicular golden rectangles
        normalize_and_scale(-1,  phi,  0),   // 0
        normalize_and_scale( 1,  phi,  0),   // 1
        normalize_and_scale(-1, -phi,  0),   // 2
        normalize_and_scale( 1, -phi,  0),   // 3

        normalize_and_scale( 0, -1,  phi),   // 4
        normalize_and_scale( 0,  1,  phi),   // 5
        normalize_and_scale( 0, -1, -phi),   // 6
        normalize_and_scale( 0,  1, -phi),   // 7

        normalize_and_scale( phi,  0, -1),   // 8
        normalize_and_scale( phi,  0,  1),   // 9
        normalize_and_scale(-phi,  0, -1),   // 10
        normalize_and_scale(-phi,  0,  1)    // 11
    };

    spdlog::info("Created {} base icosahedron vertices", base_vertices.size());

    // Create GlobeVertex objects from positions
    vertices_.reserve(base_vertices.size());
    for (size_t i = 0; i < base_vertices.size(); ++i) {
        const auto& pos = base_vertices[i];
        GlobeVertex vertex;
        vertex.position = pos;
        vertex.normal = glm::normalize(pos);  // For sphere, normal = normalized position
        vertex.geographic = PositionToGeographic(pos);
        vertex.texcoord = GeographicToUV(vertex.geographic);
        vertex.lod_level = 0;
        vertex.edge_flags = 0;
        vertices_.push_back(vertex);
    }

    // Define 20 faces of standard icosahedron with consistent CCW winding
    // Verified to produce outward-facing normals
    std::vector<std::array<int, 3>> faces = {
        // Top cap (5 triangles around vertex 5)
        {5, 11, 0}, {5, 0, 1}, {5, 1, 9}, {5, 9, 4}, {5, 4, 11},

        // Upper belt (5 triangles)
        {0, 11, 10}, {0, 10, 7}, {1, 0, 7}, {1, 7, 8}, {9, 1, 8},

        // Lower belt (5 triangles)
        {4, 9, 3}, {9, 8, 3}, {8, 7, 6}, {7, 10, 6}, {10, 11, 2},

        // Bottom cap (5 triangles around vertex 2)
        {2, 4, 3}, {2, 3, 6}, {2, 6, 10}, {2, 11, 4}, {3, 8, 6}
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
    // Use centralized CoordinateMapper for cartesian-to-geographic conversion
    using namespace coordinates;
    Geographic geo = CoordinateMapper::CartesianToGeographic(position);

    // Return as glm::vec2 (lon, lat) for compatibility with existing API
    return glm::vec2(
        static_cast<float>(geo.longitude),
        static_cast<float>(geo.latitude)
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
    (void)view_matrix;  // Reserved for future frustum-based LOD

    if (!params_.enable_adaptive) {
        return true;  // Adaptive LOD disabled, nothing to do
    }

    // Track whether mesh was regenerated (for potential GPU buffer updates)
    [[maybe_unused]] bool mesh_changed = false;

    // Calculate camera distance from globe center
    // Note: mesh uses normalized radius 1.0, not WGS84 radius
    const float camera_distance = glm::length(camera_position);
    const float normalized_radius = 1.0f;  // Mesh uses unit sphere

    // Determine target subdivision level based on camera distance
    // Closer camera = higher subdivision (more detail)
    std::uint8_t target_level = 0;
    if (camera_distance < normalized_radius * 1.01f) {
        target_level = params_.max_subdivision_level;  // Very close - max detail
    } else if (camera_distance < normalized_radius * 1.1f) {
        target_level = std::min(static_cast<std::uint8_t>(params_.max_subdivision_level - 1),
                               static_cast<std::uint8_t>(7));
    } else if (camera_distance < normalized_radius * 1.5f) {
        target_level = std::min(static_cast<std::uint8_t>(params_.max_subdivision_level - 2),
                               static_cast<std::uint8_t>(5));
    } else if (camera_distance < normalized_radius * 3.0f) {
        target_level = std::min(static_cast<std::uint8_t>(params_.max_subdivision_level - 3),
                               static_cast<std::uint8_t>(4));
    } else if (camera_distance < normalized_radius * 10.0f) {
        target_level = 3;
    } else {
        target_level = 2;  // Far away - low detail
    }

    // Clamp to valid range
    target_level = std::max(target_level, static_cast<std::uint8_t>(1));
    target_level = std::min(target_level, params_.max_subdivision_level);

    // Check if we need to regenerate the mesh at a different detail level
    // by examining the current average LOD level
    std::uint8_t current_avg_level = 0;
    if (!triangles_.empty()) {
        std::uint32_t total_level = 0;
        for (const auto& tri : triangles_) {
            total_level += tri.lod_level;
        }
        current_avg_level = static_cast<std::uint8_t>(total_level / triangles_.size());
    }

    // If target level differs significantly from current, regenerate
    if (std::abs(static_cast<int>(target_level) - static_cast<int>(current_avg_level)) >= 1) {
        spdlog::debug("Adaptive LOD: changing from level {} to {} (camera distance: {:.2f})",
                     current_avg_level, target_level, camera_distance);

        // Store old params and update subdivision level
        GlobeMeshParams old_params = params_;
        params_.max_subdivision_level = target_level;

        // Clear current mesh
        vertices_.clear();
        triangles_.clear();
        midpoint_cache_.clear();

        // Regenerate mesh at new detail level
        GenerateIcosahedron();
        SubdivideToLevel(target_level);
        GenerateVertexIndices();

        // Restore max subdivision level for future reference
        params_.max_subdivision_level = old_params.max_subdivision_level;

        mesh_changed = true;
        spdlog::info("Adaptive LOD: mesh regenerated with {} vertices, {} triangles at level {}",
                    vertices_.size(), triangles_.size(), target_level);
    }

    // Update triangle visibility and screen errors for fine-grained LOD
    for (auto& triangle : triangles_) {
        triangle.screen_error = CalculateScreenError(triangle, camera_position,
                                                     view_matrix, projection_matrix, viewport_size);
        // Mark triangles that are facing the camera as visible
        glm::vec3 triangle_center = (vertices_[triangle.vertices[0]].position +
                                    vertices_[triangle.vertices[1]].position +
                                    vertices_[triangle.vertices[2]].position) / 3.0f;
        glm::vec3 to_camera = glm::normalize(camera_position - triangle_center);
        glm::vec3 triangle_normal = glm::normalize(triangle_center);  // For sphere, normal = center
        triangle.visible = glm::dot(to_camera, triangle_normal) > -0.2f;  // Allow some backface
    }

    return true;
}

float IcosahedronGlobeMesh::CalculateScreenError(const GlobeTriangle& triangle,
                                                 const glm::vec3& camera_position,
                                                 const glm::mat4& view_matrix,
                                                 const glm::mat4& projection_matrix,
                                                 const glm::vec2& viewport_size) const {
    (void)view_matrix;  // Reserved for view-space calculations

    // Calculate triangle center in world space
    const glm::vec3& v0 = vertices_[triangle.vertices[0]].position;
    const glm::vec3& v1 = vertices_[triangle.vertices[1]].position;
    const glm::vec3& v2 = vertices_[triangle.vertices[2]].position;

    glm::vec3 center = (v0 + v1 + v2) / 3.0f;

    // Calculate the geometric error (edge length in world space)
    float edge1 = glm::length(v1 - v0);
    float edge2 = glm::length(v2 - v1);
    float edge3 = glm::length(v0 - v2);
    float max_edge = std::max({edge1, edge2, edge3});

    // Calculate distance from camera to triangle center
    float distance = glm::length(camera_position - center);
    if (distance < 0.001f) {
        distance = 0.001f;  // Prevent division by zero
    }

    // Project geometric error to screen space
    // screen_error = (geometric_error * viewport_height) / (distance * 2 * tan(fov/2))
    // Simplified: screen_error ≈ geometric_error * viewport_height / (distance * fov_factor)

    // Extract approximate FOV from projection matrix
    float fov_factor = 2.0f;  // Approximation for typical 45-degree FOV
    if (projection_matrix[1][1] > 0.001f) {
        fov_factor = 2.0f / projection_matrix[1][1];
    }

    float screen_error = (max_edge * viewport_size.y) / (distance * fov_factor);

    return screen_error;
}

bool IcosahedronGlobeMesh::NeedsSubdivision(const GlobeTriangle& triangle,
                                            const glm::vec3& camera_position,
                                            const glm::mat4& view_matrix,
                                            const glm::mat4& projection_matrix,
                                            const glm::vec2& viewport_size) const {
    // Check if triangle already at maximum subdivision level
    if (triangle.lod_level >= params_.max_subdivision_level) {
        return false;
    }

    // Calculate screen-space error (view_matrix passed through to CalculateScreenError)
    float screen_error = CalculateScreenError(triangle, camera_position,
                                              view_matrix, projection_matrix, viewport_size);

    // Needs subdivision if screen error exceeds threshold
    return screen_error > params_.max_screen_error;
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
        // Simple bounding box intersection test
        const BoundingBox2D& tri_bounds = triangles_[i].bounds;
        if (tri_bounds.max.x >= bounds.min.x && tri_bounds.min.x <= bounds.max.x &&
            tri_bounds.max.y >= bounds.min.y && tri_bounds.min.y <= bounds.max.y)
         // if (tri_bounds.Intersects(triangles_[i].bounds))
        {
            triangles_in_bounds.push_back(i);
        }
    }
    
    return triangles_in_bounds;
}

} // namespace earth_map
