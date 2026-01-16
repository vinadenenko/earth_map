#pragma once

/**
 * @file frustum.h
 * @brief View frustum culling utilities
 * 
 * Defines a view frustum for efficient visibility culling in 3D space.
 * Used to determine which objects are visible to the camera.
 */

#include <earth_map/math/bounding_box.h>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>

namespace earth_map {

/**
 * @brief Plane representation for frustum culling
 */
struct Plane {
    glm::vec3 normal;   ///< Plane normal (pointing inward)
    float distance;     ///< Distance from origin along normal
    
    /**
     * @brief Default constructor
     */
    constexpr Plane() : normal(0.0f, 1.0f, 0.0f), distance(0.0f) {}
    
    /**
     * @brief Construct from normal and distance
     * 
     * @param plane_normal Plane normal (should be normalized)
     * @param plane_distance Distance from origin
     */
    constexpr Plane(const glm::vec3& plane_normal, float plane_distance)
        : normal(plane_normal), distance(plane_distance) {}
    
    /**
     * @brief Construct from three points on the plane
     * 
     * @param point1 First point
     * @param point2 Second point
     * @param point3 Third point
     */
    static Plane FromPoints(const glm::vec3& point1, 
                           const glm::vec3& point2, 
                           const glm::vec3& point3) {
        const glm::vec3 edge1 = point2 - point1;
        const glm::vec3 edge2 = point3 - point1;
        const glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
        return Plane(normal, -glm::dot(normal, point1));
    }
    
    /**
     * @brief Normalize the plane
     */
    void Normalize() {
        const float length = glm::length(normal);
        if (length > 0.0f) {
            normal /= length;
            distance /= length;
        }
    }
    
    /**
     * @brief Get the signed distance from a point to the plane
     * 
     * @param point Point to test
     * @return float Signed distance (positive = in front of plane)
     */
    float DistanceTo(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
    
    /**
     * @brief Check if a point is in front of the plane
     * 
     * @param point Point to test
     * @return true if point is in front of or on the plane, false otherwise
     */
    bool IsInFront(const glm::vec3& point) const {
        return DistanceTo(point) >= 0.0f;
    }
};

/**
 * @brief View frustum for culling
 */
struct Frustum {
    enum PlaneIndex {
        LEFT = 0,     ///< Left clipping plane
        RIGHT = 1,    ///< Right clipping plane
        BOTTOM = 2,   ///< Bottom clipping plane
        TOP = 3,      ///< Top clipping plane
        NEAR = 4,     ///< Near clipping plane
        FAR = 5,      ///< Far clipping plane
        COUNT = 6     ///< Total number of planes
    };
    
    std::array<Plane, COUNT> planes;  ///< Frustum clipping planes
    
    /**
     * @brief Default constructor
     */
    Frustum() = default;
    
    /**
     * @brief Construct from view-projection matrix
     * 
     * @param view_projection Combined view and projection matrix
     */
    explicit Frustum(const glm::mat4& view_projection);
    
    /**
     * @brief Update frustum from view-projection matrix
     * 
     * @param view_projection Combined view and projection matrix
     */
    void Update(const glm::mat4& view_projection);
    
    /**
     * @brief Check if a point is inside the frustum
     * 
     * @param point Point to test
     * @return true if point is inside frustum, false otherwise
     */
    bool Contains(const glm::vec3& point) const;
    
    /**
     * @brief Check if a bounding box is inside the frustum
     * 
     * @param box Bounding box to test
     * @return true if box is inside or intersects frustum, false otherwise
     */
    bool Intersects(const BoundingBox& box) const;
    
    /**
     * @brief Check if a bounding sphere is inside the frustum
     * 
     * @param center Sphere center
     * @param radius Sphere radius
     * @return true if sphere is inside or intersects frustum, false otherwise
     */
    bool Intersects(const glm::vec3& center, float radius) const;
    
    /**
     * @brief Get frustum corners
     * 
     * @param near_distance Near plane distance
     * @param far_distance Far plane distance
     * @return std::array<glm::vec3, 8> Array of 8 corner points
     */
    std::array<glm::vec3, 8> GetCorners(float near_distance, float far_distance) const;
    
    /**
     * @brief Check if the frustum is valid
     * 
     * @return true if frustum planes are valid, false otherwise
     */
    bool IsValid() const;
    
    /**
     * @brief Normalize all planes in the frustum
     */
    void NormalizePlanes();
    
    /**
     * @brief Get a specific plane
     * 
     * @param index Plane index (PlaneIndex enum)
     * @return const Plane& Reference to the plane
     */
    const Plane& GetPlane(PlaneIndex index) const {
        return planes[index];
    }
    
    /**
     * @brief Get all planes
     * 
     * @return const std::array<Plane, COUNT>& Array of all planes
     */
    const std::array<Plane, COUNT>& GetPlanes() const {
        return planes;
    }
};

/**
 * @brief Frustum culling utilities
 */
class FrustumCuller {
public:
    /**
     * @brief Test intersection between frustum and bounding box
     * 
     * @param frustum View frustum
     * @param box Bounding box to test
     * @return bool true if box is visible, false otherwise
     */
    static bool IsVisible(const Frustum& frustum, const BoundingBox& box);
    
    /**
     * @brief Test intersection between frustum and bounding sphere
     * 
     * @param frustum View frustum
     * @param center Sphere center
     * @param radius Sphere radius
     * @return bool true if sphere is visible, false otherwise
     */
    static bool IsVisible(const Frustum& frustum, const glm::vec3& center, float radius);
    
    /**
     * @brief Test intersection between frustum and point
     * 
     * @param frustum View frustum
     * @param point Point to test
     * @return bool true if point is visible, false otherwise
     */
    static bool IsVisible(const Frustum& frustum, const glm::vec3& point);
    
    /**
     * @brief Classify bounding box relative to frustum
     * 
     * @param frustum View frustum
     * @param box Bounding box to classify
     * @return int Classification: 1 = inside, 0 = intersecting, -1 = outside
     */
    static int Classify(const Frustum& frustum, const BoundingBox& box);
    
    /**
     * @brief Classify bounding sphere relative to frustum
     * 
     * @param frustum View frustum
     * @param center Sphere center
     * @param radius Sphere radius
     * @return int Classification: 1 = inside, 0 = intersecting, -1 = outside
     */
    static int Classify(const Frustum& frustum, const glm::vec3& center, float radius);
};

} // namespace earth_map