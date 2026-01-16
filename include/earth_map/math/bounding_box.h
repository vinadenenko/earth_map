#pragma once

/**
 * @file bounding_box.h
 * @brief Bounding box and spatial query utilities
 * 
 * Defines axis-aligned bounding boxes and related spatial operations
 * for culling and intersection testing.
 */

#include <glm/glm.hpp>
#include <array>
#include <limits>
#include <cstdint>

namespace earth_map {

/**
 * @brief 3D axis-aligned bounding box
 */
struct BoundingBox {
    glm::vec3 min;  ///< Minimum corner of the box
    glm::vec3 max;  ///< Maximum corner of the box
    
    /**
     * @brief Default constructor - creates invalid bounding box
     */
    constexpr BoundingBox() 
        : min(std::numeric_limits<float>::max())
        , max(std::numeric_limits<float>::lowest()) {
    }
    
    /**
     * @brief Construct from min and max points
     * 
     * @param min_point Minimum corner
     * @param max_point Maximum corner
     */
    constexpr BoundingBox(const glm::vec3& min_point, const glm::vec3& max_point)
        : min(min_point), max(max_point) {
    }
    
    /**
     * @brief Construct from center and size
     * 
     * @param center Center point
     * @param size Size in each dimension
     */
    static BoundingBox FromCenterSize(const glm::vec3& center, const glm::vec3& size) {
        const glm::vec3 half_size = size * 0.5f;
        return BoundingBox(center - half_size, center + half_size);
    }
    
    /**
     * @brief Get the center of the bounding box
     * 
     * @return glm::vec3 Center point
     */
    constexpr glm::vec3 GetCenter() const {
        return (min + max) * 0.5f;
    }
    
    /**
     * @brief Get the size (extent) of the bounding box
     * 
     * @return glm::vec3 Size in each dimension
     */
    constexpr glm::vec3 GetSize() const {
        return max - min;
    }
    
    /**
     * @brief Get the radius (half the diagonal length)
     * 
     * @return float Bounding sphere radius
     */
    float GetRadius() const {
        return glm::length(GetSize()) * 0.5f;
    }
    
    /**
     * @brief Check if the bounding box is valid
     * 
     * @return true if valid (min <= max in all dimensions), false otherwise
     */
    constexpr bool IsValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }
    
    /**
     * @brief Check if a point is inside the bounding box
     * 
     * @param point Point to test
     * @return true if point is inside or on boundary, false otherwise
     */
    constexpr bool Contains(const glm::vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }
    
    /**
     * @brief Check if another bounding box intersects with this one
     * 
     * @param other Other bounding box
     * @return true if boxes intersect, false otherwise
     */
    constexpr bool Intersects(const BoundingBox& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }
    
    /**
     * @brief Expand the bounding box to include a point
     * 
     * @param point Point to include
     */
    void Enclose(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }
    
    /**
     * @brief Expand the bounding box to include another bounding box
     * 
     * @param other Other bounding box to include
     */
    void Enclose(const BoundingBox& other) {
        if (other.IsValid()) {
            min = glm::min(min, other.min);
            max = glm::max(max, other.max);
        }
    }
    
    /**
     * @brief Transform the bounding box by a matrix
     * 
     * @param matrix Transformation matrix
     * @return BoundingBox Transformed bounding box
     */
    BoundingBox Transform(const glm::mat4& matrix) const;
    
    /**
     * @brief Get the 8 corners of the bounding box
     * 
     * @return std::array<glm::vec3, 8> Array of corner points
     */
    std::array<glm::vec3, 8> GetCorners() const {
        return {
            glm::vec3(min.x, min.y, min.z),
            glm::vec3(max.x, min.y, min.z),
            glm::vec3(min.x, max.y, min.z),
            glm::vec3(max.x, max.y, min.z),
            glm::vec3(min.x, min.y, max.z),
            glm::vec3(max.x, min.y, max.z),
            glm::vec3(min.x, max.y, max.z),
            glm::vec3(max.x, max.y, max.z)
        };
    }
    
    /**
     * @brief Calculate the volume of the bounding box
     * 
     * @return float Volume (0 if invalid)
     */
    float GetVolume() const {
        if (!IsValid()) return 0.0f;
        const glm::vec3 size = GetSize();
        return size.x * size.y * size.z;
    }
    
    /**
     * @brief Calculate the surface area of the bounding box
     * 
     * @return float Surface area (0 if invalid)
     */
    float GetSurfaceArea() const {
        if (!IsValid()) return 0.0f;
        const glm::vec3 size = GetSize();
        return 2.0f * (size.x * size.y + size.x * size.z + size.y * size.z);
    }
    
    /**
     * @brief Reset to invalid state
     */
    void Reset() {
        min = glm::vec3(std::numeric_limits<float>::max());
        max = glm::vec3(std::numeric_limits<float>::lowest());
    }
};

/**
 * @brief 2D axis-aligned bounding box (for geographic coordinates)
 */
struct BoundingBox2D {
    glm::vec2 min;  ///< Minimum corner (longitude, latitude)
    glm::vec2 max;  ///< Maximum corner (longitude, latitude)
    
    /**
     * @brief Default constructor - creates invalid bounding box
     */
    constexpr BoundingBox2D()
        : min(std::numeric_limits<float>::max())
        , max(std::numeric_limits<float>::lowest()) {
    }
    
    /**
     * @brief Construct from min and max points
     * 
     * @param min_point Minimum corner
     * @param max_point Maximum corner
     */
    constexpr BoundingBox2D(const glm::vec2& min_point, const glm::vec2& max_point)
        : min(min_point), max(max_point) {
    }
    
    /**
     * @brief Check if the bounding box is valid
     * 
     * @return true if valid, false otherwise
     */
    constexpr bool IsValid() const {
        return min.x <= max.x && min.y <= max.y;
    }
    
    /**
     * @brief Check if a point is inside the bounding box
     * 
     * @param point Point to test
     * @return true if point is inside or on boundary, false otherwise
     */
    constexpr bool Contains(const glm::vec2& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y;
    }
    
    /**
     * @brief Check if another bounding box intersects with this one
     * 
     * @param other Other bounding box
     * @return true if boxes intersect, false otherwise
     */
    constexpr bool Intersects(const BoundingBox2D& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y;
    }
    
    /**
     * @brief Get the center of the bounding box
     * 
     * @return glm::vec2 Center point
     */
    constexpr glm::vec2 GetCenter() const {
        return (min + max) * 0.5f;
    }
    
    /**
     * @brief Get the size of the bounding box
     * 
     * @return glm::vec2 Size in each dimension
     */
    constexpr glm::vec2 GetSize() const {
        return max - min;
    }
};

} // namespace earth_map