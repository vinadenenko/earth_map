#include "earth_map/math/frustum.h"
#include <glm/gtc/matrix_inverse.hpp>

namespace earth_map {

void Frustum::Update(const glm::mat4& view_projection) {
    view_projection_matrix_ = view_projection;

    // Same as constructor
    planes[LEFT].normal = glm::vec3(view_projection[0][3] + view_projection[0][0],
                                   view_projection[1][3] + view_projection[1][0],
                                   view_projection[2][3] + view_projection[2][0]);
    planes[LEFT].distance = view_projection[3][3] + view_projection[3][0];

    planes[RIGHT].normal = glm::vec3(view_projection[0][3] - view_projection[0][0],
                                    view_projection[1][3] - view_projection[1][0],
                                    view_projection[2][3] - view_projection[2][0]);
    planes[RIGHT].distance = view_projection[3][3] - view_projection[3][0];

    planes[BOTTOM].normal = glm::vec3(view_projection[0][3] + view_projection[0][1],
                                     view_projection[1][3] + view_projection[1][1],
                                     view_projection[2][3] + view_projection[2][1]);
    planes[BOTTOM].distance = view_projection[3][3] + view_projection[3][1];

    planes[TOP].normal = glm::vec3(view_projection[0][3] - view_projection[0][1],
                                  view_projection[1][3] - view_projection[1][1],
                                  view_projection[2][3] - view_projection[2][1]);
    planes[TOP].distance = view_projection[3][3] - view_projection[3][1];

    planes[NEAR].normal = glm::vec3(view_projection[0][3] + view_projection[0][2],
                                   view_projection[1][3] + view_projection[1][2],
                                   view_projection[2][3] + view_projection[2][2]);
    planes[NEAR].distance = view_projection[3][3] + view_projection[3][2];

    planes[FAR].normal = glm::vec3(view_projection[0][3] - view_projection[0][2],
                                  view_projection[1][3] - view_projection[1][2],
                                  view_projection[2][3] - view_projection[2][2]);
    planes[FAR].distance = view_projection[3][3] - view_projection[3][2];

    // Normalize all plane normals
    for (auto& plane : planes) {
        plane.normal = glm::normalize(plane.normal);
    }
}

bool Frustum::Contains(const glm::vec3& point) const {
    for (const auto& plane : planes) {
        if (!plane.IsInFront(point)) {
            return false;
        }
    }
    return true;
}

bool Frustum::Intersects(const BoundingBox& box) const {
    // TODO: Implement bounding box intersection test
    // For now, just check if any corner is inside
    const auto corners = box.GetCorners();
    for (const auto& corner : corners) {
        if (Contains(corner)) {
            return true;
        }
    }
    return false;
}

std::array<glm::vec3, 8> Frustum::GetCorners(float near_distance, float far_distance) const {
    (void)near_distance;
    (void)far_distance;
    std::array<glm::vec3, 8> corners;

    // Compute inverse view-projection matrix
    glm::mat4 inv_vp = glm::inverse(view_projection_matrix_);

    // NDC corners in order: near bottom-left, near bottom-right, near top-right, near top-left,
    // far bottom-left, far bottom-right, far top-right, far top-left
    std::array<glm::vec4, 8> ndc_corners = {{
        {-1.0f, -1.0f, -1.0f, 1.0f}, // near bottom-left
        { 1.0f, -1.0f, -1.0f, 1.0f}, // near bottom-right
        { 1.0f,  1.0f, -1.0f, 1.0f}, // near top-right
        {-1.0f,  1.0f, -1.0f, 1.0f}, // near top-left
        {-1.0f, -1.0f,  1.0f, 1.0f}, // far bottom-left
        { 1.0f, -1.0f,  1.0f, 1.0f}, // far bottom-right
        { 1.0f,  1.0f,  1.0f, 1.0f}, // far top-right
        {-1.0f,  1.0f,  1.0f, 1.0f}  // far top-left
    }};

    // Transform NDC corners to world space
    for (size_t i = 0; i < 8; ++i) {
        glm::vec4 world_homogeneous = inv_vp * ndc_corners[i];
        corners[i] = glm::vec3(world_homogeneous) / world_homogeneous.w;
    }

    return corners;
}

void Frustum::NormalizePlanes() {
    for (auto& plane : planes) {
        plane.Normalize();
    }
}

} // namespace earth_map
