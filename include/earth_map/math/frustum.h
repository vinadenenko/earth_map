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


} // namespace earth_map
