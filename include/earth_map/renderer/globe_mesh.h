#pragma once

/**
 * @file globe_mesh.h
 * @brief Globe mesh generation with icosahedron-based tessellation
 * 
 * Provides icosahedron-based sphere mesh generation with adaptive
 * subdivision for level-of-detail rendering. Supports proper UV
 * coordinate generation and normal calculation for realistic Earth rendering.
 */

#include <earth_map/math/bounding_box.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <unordered_map>

namespace earth_map {

// Forward declaration
class ElevationManager;

/**
 * @brief Vertex structure for globe mesh
 */
struct GlobeVertex {
    /** Vertex position in 3D space */
    glm::vec3 position;
    
    /** Surface normal at vertex */
    glm::vec3 normal;
    
    /** Texture coordinates (U, V) */
    glm::vec2 texcoord;
    
    /** Geographic coordinates (longitude, latitude) */
    glm::vec2 geographic;

    /** Pre-computed tile coordinates at base zoom level (for spherical mapping) */
    glm::ivec2 base_tile = glm::ivec2(0, 0);

    /** Fractional position within tile [0,1] (for spherical mapping) */
    glm::vec2 tile_frac = glm::vec2(0.0f, 0.0f);

    /** Subdivision level of this vertex */
    std::uint8_t lod_level = 0;
    
    /** Edge flags for crack prevention */
    std::uint8_t edge_flags = 0;
    
    bool operator==(const GlobeVertex& other) const {
        const float epsilon = 1e-6f;
        return glm::length(position - other.position) < epsilon &&
               glm::length(normal - other.normal) < epsilon &&
               glm::length(texcoord - other.texcoord) < epsilon;
    }
};

/**
 * @brief Triangle face for globe mesh
 */
struct GlobeTriangle {
    /** Indices of the three vertices */
    std::uint32_t vertices[3];
    
    /** Subdivision level of this triangle */
    std::uint8_t lod_level = 0;
    
    /** Screen-space error for this triangle */
    float screen_error = 0.0f;
    
    /** Geographic bounds of this triangle */
    BoundingBox2D bounds;
    
    /** Whether this triangle is visible */
    bool visible = true;
};

/**
 * @brief Mesh quality levels for adaptive tessellation
 */
enum class MeshQuality {
    LOW,      ///< Low detail, minimal triangles
    MEDIUM,   ///< Balanced detail and performance
    HIGH,     ///< High detail for close viewing
    ULTRA     ///< Maximum detail for analysis
};

/**
 * @brief Globe mesh generation parameters
 */
struct GlobeMeshParams {
    /** Base radius of the globe in meters */
    double radius = 6378137.0;  // WGS84 semi-major axis
    
    /** Maximum subdivision level */
    std::uint8_t max_subdivision_level = 8;
    
    /** Target number of base vertices (icosahedron = 12) */
    std::size_t base_vertices = 12;
    
    /** Enable adaptive subdivision based on camera distance */
    bool enable_adaptive = true;
    
    /** Maximum screen-space error in pixels */
    float max_screen_error = 2.0f;
    
    /** Mesh quality setting */
    MeshQuality quality = MeshQuality::MEDIUM;
    
    /** Enable crack prevention between subdivision levels */
    bool enable_crack_prevention = true;
    
    /** Minimum subdivision level for coastal areas */
    std::uint8_t min_coastal_level = 4;
    
    /** Maximum edge length for subdivision in meters */
    double max_edge_length = 100000.0;  // 100km
};

/**
 * @brief Globe mesh generator
 * 
 * Generates icosahedron-based sphere meshes with adaptive subdivision
 * for level-of-detail rendering. Provides efficient mesh generation
 * with proper UV coordinates, normals, and geographic mapping.
 */
class GlobeMesh {
public:
    /**
     * @brief Create a new globe mesh instance
     * 
     * @param params Mesh generation parameters
     * @return std::unique_ptr<GlobeMesh> New globe mesh instance
     */
    static std::unique_ptr<GlobeMesh> Create(const GlobeMeshParams& params = {});
    
    /**
     * @brief Virtual destructor
     */
    virtual ~GlobeMesh() = default;
    
    /**
     * @brief Generate the globe mesh
     * 
     * Generates the complete globe mesh with adaptive subdivision
     * based on the provided parameters.
     * 
     * @return true if mesh generation succeeded, false otherwise
     */
    virtual bool Generate() = 0;
    
    /**
     * @brief Update mesh based on camera position
     * 
     * Updates the mesh subdivision based on current camera position
     * and viewing parameters for adaptive LOD.
     * 
     * @param camera_position Camera position in world coordinates
     * @param view_matrix Current view matrix
     * @param projection_matrix Current projection matrix
     * @param viewport_size Viewport dimensions (width, height)
     * @return true if update succeeded, false otherwise
     */
    virtual bool UpdateLOD(const glm::vec3& camera_position,
                           const glm::mat4& view_matrix,
                           const glm::mat4& projection_matrix,
                           const glm::vec2& viewport_size) = 0;
    
    /**
     * @brief Get mesh vertices
     * 
     * @return const std::vector<GlobeVertex>& Vertex data
     */
    virtual const std::vector<GlobeVertex>& GetVertices() const = 0;
    
    /**
     * @brief Get mesh triangles
     * 
     * @return const std::vector<GlobeTriangle>& Triangle data
     */
    virtual const std::vector<GlobeTriangle>& GetTriangles() const = 0;
    
    /**
     * @brief Get vertex indices for rendering
     * 
     * @return std::vector<std::uint32_t> Vertex indices for OpenGL rendering
     */
    virtual std::vector<std::uint32_t> GetVertexIndices() const = 0;
    
    /**
     * @brief Get mesh parameters
     * 
     * @return GlobeMeshParams Current mesh parameters
     */
    virtual GlobeMeshParams GetParameters() const = 0;
    
    /**
     * @brief Set mesh quality
     * 
     * @param quality New quality setting
     */
    virtual void SetQuality(MeshQuality quality) = 0;
    
    /**
     * @brief Get current mesh quality
     * 
     * @return MeshQuality Current quality setting
     */
    virtual MeshQuality GetQuality() const = 0;
    
    /**
     * @brief Get mesh statistics
     * 
     * @return std::pair<std::size_t, std::size_t> Number of (vertices, triangles)
     */
    virtual std::pair<std::size_t, std::size_t> GetStatistics() const = 0;
    
    /**
     * @brief Validate mesh integrity
     * 
     * @return true if mesh is valid, false otherwise
     */
    virtual bool Validate() const = 0;
    
    /**
     * @brief Optimize mesh for GPU rendering
     * 
     * Optimizes vertex ordering and triangle connectivity for
     * improved GPU cache performance.
     * 
     * @return true if optimization succeeded, false otherwise
     */
    virtual bool Optimize() = 0;
    
    /**
     * @brief Calculate geographic bounds of the mesh
     * 
     * @return BoundingBox2D Geographic bounds covering the mesh
     */
    virtual BoundingBox2D CalculateBounds() const = 0;
    
    /**
     * @brief Find vertices at specific geographic location
     * 
     * @param longitude Longitude in degrees
     * @param latitude Latitude in degrees
     * @param radius Search radius in meters
     * @return std::vector<std::size_t> Indices of nearby vertices
     */
    virtual std::vector<std::size_t> FindVerticesAtLocation(
        double longitude, double latitude, double radius = 1000.0) const = 0;
    
    /**
     * @brief Get triangles intersecting geographic bounds
     * 
     * @param bounds Geographic bounds to query
     * @return std::vector<std::size_t> Indices of intersecting triangles
     */
    virtual std::vector<std::size_t> GetTrianglesInBounds(
        const BoundingBox2D& bounds) const = 0;

protected:
    /**
     * @brief Protected constructor
     */
    GlobeMesh() = default;
};

/**
 * @brief Icosahedron-based globe mesh implementation
 * 
 * Implements globe mesh generation using icosahedron subdivision
 * with adaptive tessellation for optimal performance.
 */
class IcosahedronGlobeMesh : public GlobeMesh {
public:
    explicit IcosahedronGlobeMesh(const GlobeMeshParams& params);
    
    bool Generate() override;
    bool UpdateLOD(const glm::vec3& camera_position,
                   const glm::mat4& view_matrix,
                   const glm::mat4& projection_matrix,
                   const glm::vec2& viewport_size) override;
    
    const std::vector<GlobeVertex>& GetVertices() const override;
    const std::vector<GlobeTriangle>& GetTriangles() const override;
    std::vector<std::uint32_t> GetVertexIndices() const override;
    
    GlobeMeshParams GetParameters() const override;
    void SetQuality(MeshQuality quality) override;
    MeshQuality GetQuality() const override;
    
    std::pair<std::size_t, std::size_t> GetStatistics() const override;
    bool Validate() const override;
    bool Optimize() override;
    BoundingBox2D CalculateBounds() const override;
    
    std::vector<std::size_t> FindVerticesAtLocation(
        double longitude, double latitude, double radius = 1000.0) const override;
    std::vector<std::size_t> GetTrianglesInBounds(
        const BoundingBox2D& bounds) const override;

    /**
     * @brief Set elevation manager for terrain displacement
     *
     * @param manager Shared pointer to elevation manager
     */
    void SetElevationManager(std::shared_ptr<ElevationManager> manager);

private:
    GlobeMeshParams params_;
    std::vector<GlobeVertex> vertices_;
    std::vector<GlobeTriangle> triangles_;
    std::vector<std::uint32_t> vertex_indices_;
    std::unordered_map<std::uint64_t, std::size_t> midpoint_cache_;
    std::shared_ptr<ElevationManager> elevation_manager_;

    /**
     * @brief Generate icosahedron base mesh
     */
    void GenerateIcosahedron();

    /**
     * @brief Apply elevation data to mesh vertices
     *
     * Called after tessellation to displace vertices based on elevation data
     */
    void ApplyElevation();
    
    /**
     * @brief Subdivide triangle recursively
     */
    void SubdivideTriangle(const GlobeTriangle& triangle, std::uint8_t target_level);
    
    /**
     * @brief Subdivide mesh to target level
     */
    void SubdivideToLevel(std::uint8_t target_level);
    
    /**
     * @brief Create vertex at midpoint of edge
     */
    std::size_t CreateMidpointVertex(std::size_t v1, std::size_t v2);
    
    /**
     * @brief Generate vertex indices for rendering
     */
    void GenerateVertexIndices();
    
    /**
     * @brief Calculate vertex geographic coordinates
     */
    glm::vec2 PositionToGeographic(const glm::vec3& position) const;
    
    /**
     * @brief Calculate UV coordinates from geographic coordinates
     */
    glm::vec2 GeographicToUV(const glm::vec2& geographic) const;
    
    /**
     * @brief Calculate screen-space error for triangle
     */
    float CalculateScreenError(const GlobeTriangle& triangle,
                              const glm::vec3& camera_position,
                              const glm::mat4& view_matrix,
                              const glm::mat4& projection_matrix,
                              const glm::vec2& viewport_size) const;
    
    /**
     * @brief Check if triangle needs subdivision
     */
    bool NeedsSubdivision(const GlobeTriangle& triangle,
                         const glm::vec3& camera_position,
                         const glm::mat4& view_matrix,
                         const glm::mat4& projection_matrix,
                         const glm::vec2& viewport_size) const;
    
    /**
     * @brief Prevent cracks between subdivision levels
     */
    void PreventCracks();
    
    /**
     * @brief Optimize vertex ordering for GPU cache
     */
    void OptimizeVertexOrder();
    
    /**
     * @brief Calculate triangle geographic bounds
     */
    BoundingBox2D CalculateTriangleBounds(const GlobeTriangle& triangle) const;
};

} // namespace earth_map