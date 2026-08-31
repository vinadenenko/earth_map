/**
 * @file point_placemark_renderer.cpp
 * @brief See point_placemark_renderer.h.
 */

#include "point_placemark_renderer.h"

#include "../ecef_render_frame.h"

#include <earth_map/renderer/shader_loader.h>

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <variant>

namespace earth_map::renderer::placemarks {

namespace {

using earth_map::placemarks::IconAtlasEntry;
using earth_map::placemarks::PlacemarkIconRegistry;
using earth_map::placemarks::PlacemarkIconSnapshot;
using earth_map::placemarks::PlacemarkSnapshot;
using earth_map::placemarks::PointPlacemark;

// PlacemarkIconRegistry::kMaxIconCount (64) icons laid out in an 8x8 grid of
// kIconCellSize (64px) cells -- 8*8 == 64 exactly, so every registered icon
// has a home with no wasted slots and no bin-packing needed.
constexpr std::uint32_t kAtlasGridSize = 8;
constexpr std::uint32_t kAtlasTexturePixels =
    kAtlasGridSize * PlacemarkIconRegistry::kIconCellSize;

struct AtlasUvRect final {
    glm::vec2 minimum;
    glm::vec2 maximum;
};

[[nodiscard]] AtlasUvRect ComputeAtlasUvRect(const IconAtlasEntry& entry) noexcept {
    const std::uint32_t column = entry.atlas_slot % kAtlasGridSize;
    const std::uint32_t row = entry.atlas_slot / kAtlasGridSize;
    const float cell = 1.0f / static_cast<float>(kAtlasGridSize);
    const glm::vec2 minimum(static_cast<float>(column) * cell, static_cast<float>(row) * cell);
    return {minimum, minimum + glm::vec2(cell, cell)};
}

/**
 * Gribb-Hartmann frustum-plane extraction from a combined view-projection
 * matrix, in whatever space that matrix transforms from -- here, the
 * camera-relative local ENU space every other direct-render path in this
 * renderer already uses (view_matrix always places the camera at the local
 * origin by construction, see CameraImpl::UpdateViewMatrix).
 */
[[nodiscard]] std::array<glm::vec4, 6> ExtractLocalFrustumPlanes(
    const glm::mat4& view_projection) noexcept {
    const auto row = [&view_projection](int index) {
        return glm::vec4(view_projection[0][index], view_projection[1][index],
                         view_projection[2][index], view_projection[3][index]);
    };
    const glm::vec4 row0 = row(0);
    const glm::vec4 row1 = row(1);
    const glm::vec4 row2 = row(2);
    const glm::vec4 row3 = row(3);

    std::array<glm::vec4, 6> planes = {
        row3 + row0,  // left
        row3 - row0,  // right
        row3 + row1,  // bottom
        row3 - row1,  // top
        row3 + row2,  // near
        row3 - row2,  // far
    };
    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1e-8f) {
            plane /= length;
        }
    }
    return planes;
}

/**
 * Ports the local-space frustum planes above into ECEF, matching
 * placemarks::internal::EcefPlane's convention (normal points into the
 * visible half-space, dot(normal, point) + distance >= 0 means visible).
 * Local direction d, rotated back to ECEF, is FromLocal(d) - origin: FromLocal
 * itself both rotates and translates by the frame's origin, and translation
 * has no meaning for a direction, so subtracting the origin back out isolates
 * the rotation alone.
 */
[[nodiscard]] earth_map::placemarks::internal::EcefFrustum ExtractEcefFrustum(
    const glm::mat4& view_matrix,
    const glm::mat4& projection_matrix,
    const EcefRenderFrame& frame) noexcept {
    const std::array<glm::vec4, 6> local_planes =
        ExtractLocalFrustumPlanes(projection_matrix * view_matrix);

    earth_map::placemarks::internal::EcefFrustum frustum;
    for (std::size_t index = 0; index < local_planes.size(); ++index) {
        const glm::dvec3 normal_local(local_planes[index].x, local_planes[index].y,
                                      local_planes[index].z);
        const glm::dvec3 normal_ecef = glm::normalize(
            frame.FromLocal(normal_local).meters - frame.CameraOrigin().meters);
        const double distance_ecef = static_cast<double>(local_planes[index].w) -
            glm::dot(normal_ecef, frame.CameraOrigin().meters);
        frustum.planes[index] = {normal_ecef, distance_ecef};
    }
    return frustum;
}

constexpr const char* kVertexShader = EARTH_MAP_GLSL_PREAMBLE R"(
layout (location = 0) in vec2 aQuadCorner;
layout (location = 1) in vec2 aQuadUv;
layout (location = 2) in vec3 iLocalPosition;
layout (location = 3) in vec2 iAtlasUvMin;
layout (location = 4) in vec2 iAtlasUvMax;
layout (location = 5) in float iScale;
layout (location = 6) in float iRotation;
layout (location = 7) in vec4 iColor;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec2 uViewportSize;

// On-screen icon size in pixels at IconStyle::scale == 1.0. Arbitrary but
// documented: half the source 64px cell size, so a default-scale icon reads
// at roughly native resolution on a standard-DPI display.
const float kBaseIconSizePixels = 32.0;

out vec2 AtlasUv;
out vec4 InstanceColor;

void main() {
    vec4 clipPosition = uProjection * uView * vec4(iLocalPosition, 1.0);

    float cosRotation = cos(iRotation);
    float sinRotation = sin(iRotation);
    vec2 rotatedCorner = vec2(
        aQuadCorner.x * cosRotation - aQuadCorner.y * sinRotation,
        aQuadCorner.x * sinRotation + aQuadCorner.y * cosRotation
    );

    // Constant screen-space size regardless of distance: the offset is
    // scaled by clip-space w so that after the hardware's perspective
    // divide (xy / w), the w cancels out and only the pixel-derived offset
    // remains.
    vec2 pixelOffset = rotatedCorner * iScale * kBaseIconSizePixels;
    vec2 ndcOffset = (pixelOffset / uViewportSize) * 2.0;
    clipPosition.xy += ndcOffset * clipPosition.w;

    gl_Position = clipPosition;
    AtlasUv = mix(iAtlasUvMin, iAtlasUvMax, aQuadUv);
    InstanceColor = iColor;
}
)";

constexpr const char* kFragmentShader = EARTH_MAP_GLSL_PREAMBLE R"(
in vec2 AtlasUv;
in vec4 InstanceColor;

out vec4 FragColor;

uniform sampler2D uAtlas;

void main() {
    vec4 texColor = texture(uAtlas, AtlasUv);
    FragColor = texColor * InstanceColor;
    if (FragColor.a < 0.01) {
        discard;
    }
}
)";

}  // namespace

PointPlacemarkRenderer::PointPlacemarkRenderer(bool skip_gl_init)
    : skip_gl_init_(skip_gl_init) {}

PointPlacemarkRenderer::~PointPlacemarkRenderer() {
    if (skip_gl_init_) {
        return;
    }
    if (instance_vbo_) {
        glDeleteBuffers(1, &instance_vbo_);
    }
    if (quad_vbo_) {
        glDeleteBuffers(1, &quad_vbo_);
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
    }
    if (atlas_texture_) {
        glDeleteTextures(1, &atlas_texture_);
    }
    if (program_) {
        glDeleteProgram(program_);
    }
}

std::vector<PointPlacemarkInstance> PointPlacemarkRenderer::BuildInstances(
    const PlacemarkSnapshot& placemark_snapshot,
    const PlacemarkIconSnapshot& icon_snapshot,
    const geodesy::GeodeticPosition& camera_geodetic,
    const glm::mat4& view_matrix,
    const glm::mat4& projection_matrix) {
    const EcefRenderFrame frame = EcefRenderFrame::FromCamera(camera_geodetic);

    earth_map::placemarks::internal::PlacemarkSelectionInput selection_input;
    selection_input.frustum = ExtractEcefFrustum(view_matrix, projection_matrix, frame);
    selection_input.camera = frame.CameraOrigin();
    selection_input.cull_point_and_custom_horizon = true;

    const std::vector<earth_map::placemarks::internal::SelectedPlacemark> selected =
        selector_.Select(placemark_snapshot, selection_input);

    std::vector<PointPlacemarkInstance> instances;
    instances.reserve(selected.size());
    for (const auto& item : selected) {
        if (item.kind != earth_map::placemarks::internal::PlacemarkFeatureKind::Point) {
            continue;
        }
        const auto feature = placemark_snapshot.Features().find(item.id);
        if (feature == placemark_snapshot.Features().end()) {
            continue;
        }
        const auto* point = std::get_if<PointPlacemark>(&feature->second);
        if (!point) {
            continue;
        }
        const std::optional<IconAtlasEntry> atlas_entry =
            icon_snapshot.Find(point->icon.icon_key);
        if (!atlas_entry.has_value()) {
            continue;
        }

        const AtlasUvRect uv_rect = ComputeAtlasUvRect(*atlas_entry);
        PointPlacemarkInstance instance;
        instance.local_position = glm::vec3(frame.ToLocal(item.anchor));
        instance.atlas_uv_min = uv_rect.minimum;
        instance.atlas_uv_max = uv_rect.maximum;
        instance.scale = point->icon.scale;
        instance.rotation_radians = glm::radians(point->icon.rotation_degrees);
        instance.color = glm::vec4(point->icon.color[0], point->icon.color[1],
                                   point->icon.color[2], point->icon.color[3]);
        instances.push_back(instance);
    }
    return instances;
}

void PointPlacemarkRenderer::EnsureGlResources() {
    if (gl_resources_ready_) {
        return;
    }

    program_ = ShaderLoader::CreateProgram(kVertexShader, kFragmentShader,
                                           "point_placemark");
    if (program_ == 0) {
        spdlog::error("Failed to create point placemark shader program");
        return;
    }
    uniform_locs_.view = glGetUniformLocation(program_, "uView");
    uniform_locs_.projection = glGetUniformLocation(program_, "uProjection");
    uniform_locs_.viewport_size = glGetUniformLocation(program_, "uViewportSize");
    uniform_locs_.atlas = glGetUniformLocation(program_, "uAtlas");

    // Unit quad, centred at the anchor, in (corner, uv) pairs -- two
    // triangles, no index buffer needed for six vertices.
    constexpr float kQuadVertices[] = {
        // corner.x, corner.y, uv.x, uv.y
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 1.0f,
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &quad_vbo_);
    glGenBuffers(1, &instance_vbo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    const auto instance_attribute = [](GLuint location, GLint components, std::size_t offset) {
        glVertexAttribPointer(location, components, GL_FLOAT, GL_FALSE,
                              sizeof(PointPlacemarkInstance),
                              reinterpret_cast<const void*>(offset));
        glEnableVertexAttribArray(location);
        glVertexAttribDivisor(location, 1);
    };
    instance_attribute(2, 3, offsetof(PointPlacemarkInstance, local_position));
    instance_attribute(3, 2, offsetof(PointPlacemarkInstance, atlas_uv_min));
    instance_attribute(4, 2, offsetof(PointPlacemarkInstance, atlas_uv_max));
    instance_attribute(5, 1, offsetof(PointPlacemarkInstance, scale));
    instance_attribute(6, 1, offsetof(PointPlacemarkInstance, rotation_radians));
    instance_attribute(7, 4, offsetof(PointPlacemarkInstance, color));

    glBindVertexArray(0);
    gl_resources_ready_ = true;
}

void PointPlacemarkRenderer::EnsureAtlasTexture(const PlacemarkIconSnapshot& icon_snapshot) {
    if (atlas_built_ && icon_snapshot.Revision() == atlas_revision_built_) {
        return;
    }

    std::vector<std::uint8_t> atlas_pixels(
        static_cast<std::size_t>(kAtlasTexturePixels) * kAtlasTexturePixels * 4, 0);
    for (const auto& [icon_key, icon] : icon_snapshot.Icons()) {
        (void)icon_key;
        if (!icon.rgba_pixels) {
            continue;
        }
        const std::uint32_t origin_x =
            (icon.placement.atlas_slot % kAtlasGridSize) * PlacemarkIconRegistry::kIconCellSize;
        const std::uint32_t origin_y =
            (icon.placement.atlas_slot / kAtlasGridSize) * PlacemarkIconRegistry::kIconCellSize;
        for (std::uint32_t y = 0; y < icon.placement.height; ++y) {
            for (std::uint32_t x = 0; x < icon.placement.width; ++x) {
                const std::size_t source_index =
                    (static_cast<std::size_t>(y) * icon.placement.width + x) * 4;
                const std::size_t dest_index =
                    (static_cast<std::size_t>(origin_y + y) * kAtlasTexturePixels +
                     (origin_x + x)) * 4;
                if (source_index + 4 <= icon.rgba_pixels->size() &&
                    dest_index + 4 <= atlas_pixels.size()) {
                    std::memcpy(&atlas_pixels[dest_index], &(*icon.rgba_pixels)[source_index], 4);
                }
            }
        }
    }

    if (atlas_texture_ == 0) {
        glGenTextures(1, &atlas_texture_);
    }
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                static_cast<GLsizei>(kAtlasTexturePixels),
                static_cast<GLsizei>(kAtlasTexturePixels), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                atlas_pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    atlas_revision_built_ = icon_snapshot.Revision();
    atlas_built_ = true;
}

void PointPlacemarkRenderer::Render(
    const PlacemarkSnapshot& placemark_snapshot,
    const PlacemarkIconSnapshot& icon_snapshot,
    const geodesy::GeodeticPosition& camera_geodetic,
    const glm::mat4& view_matrix,
    const glm::mat4& projection_matrix,
    const glm::vec2& viewport_size_pixels) {
    if (skip_gl_init_) {
        return;
    }

    const std::vector<PointPlacemarkInstance> instances = BuildInstances(
        placemark_snapshot, icon_snapshot, camera_geodetic, view_matrix, projection_matrix);
    if (instances.empty()) {
        return;
    }

    EnsureGlResources();
    if (program_ == 0) {
        return;
    }
    EnsureAtlasTexture(icon_snapshot);

    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(instances.size() * sizeof(PointPlacemarkInstance)),
                instances.data(), GL_DYNAMIC_DRAW);

    glUseProgram(program_);
    glUniformMatrix4fv(uniform_locs_.view, 1, GL_FALSE, glm::value_ptr(view_matrix));
    glUniformMatrix4fv(uniform_locs_.projection, 1, GL_FALSE, glm::value_ptr(projection_matrix));
    glUniform2f(uniform_locs_.viewport_size, viewport_size_pixels.x, viewport_size_pixels.y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glUniform1i(uniform_locs_.atlas, 0);

    // Icons composite over the globe/sky; they do not participate in the
    // depth-sorted opaque geometry the way tile patches do, but the depth
    // test itself must stay on so an icon on the far side of the globe (a
    // placemark the horizon-culling above didn't already reject, e.g. one
    // just past the terminator) does not draw through the planet.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(instances.size()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
}

}  // namespace earth_map::renderer::placemarks
