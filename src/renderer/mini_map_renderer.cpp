#include "earth_map/renderer/mini_map_renderer.h"
#include "earth_map/core/camera_controller.h"
#include "earth_map/constants.h"
#include "earth_map/coordinates/coordinate_mapper.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <vector>
#include <array>

namespace {

// Helper function to compute ray-sphere intersection
// Returns the farthest intersection point, or vec3(0) if no intersection
// For visibility boundary calculation, farthest intersection represents far-side silhouette
glm::vec3 RaySphereIntersection(const glm::vec3& ray_origin, const glm::vec3& ray_dir,
                               const glm::vec3& sphere_center, float sphere_radius) {
    glm::vec3 oc = ray_origin - sphere_center;
    float a = glm::dot(ray_dir, ray_dir);
    float b = 2.0f * glm::dot(oc, ray_dir);
    float c = glm::dot(oc, oc) - sphere_radius * sphere_radius;
    float discriminant = b * b - 4 * a * c;

    if (discriminant < 0) {
        return glm::vec3(0.0f); // No intersection
    }

    // Use farthest intersection for visibility boundary (far-side silhouette)
    float t = (-b + std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0) {
        return glm::vec3(0.0f); // Intersection behind ray
    }

    return ray_origin + t * ray_dir;
}

// Convert world position to mini-map UV coordinates (equirectangular projection)
glm::vec2 WorldToUV(const glm::vec3& world_pos) {
    // Use CoordinateMapper for consistent geographic conversion (matches main globe)
    // Create World and Geographic objects directly with manual math matching CoordinateMapper
    glm::vec3 normalized = glm::normalize(world_pos);

    // Use same conversion as CoordinateMapper::CartesianToGeographic
    // lat = asin(y), lon = atan2(x, z) - Y is up, longitude 0° at +Z axis
    double lat_rad = std::asin(std::clamp(normalized.y, -1.0f, 1.0f));
    double lon_rad = std::atan2(normalized.x, normalized.z);

    double lat_deg = glm::degrees(lat_rad);
    double lon_deg = glm::degrees(lon_rad);

    // Convert to UV (0-1 range) - longitude from -180 to 180, latitude from -90 to 90
    // Earth texture has north at top (V=1), south at bottom (V=0)
    float u = (lon_deg / 360.0f + 0.5f); // Lon -180 to 180 -> U 0 to 1
    float v = (lat_deg / 180.0f + 0.5f);  // Lat -90 to 90 -> V 0 to 1 (north at top)

    return glm::vec2(u, v);
}

}

namespace earth_map {

MiniMapRenderer::MiniMapRenderer(const Config& config) : config_(config) {
    spdlog::info("Creating mini-map renderer: {}x{} pixels", config.width, config.height);
}

MiniMapRenderer::~MiniMapRenderer() {
    // Cleanup OpenGL resources
    if (framebuffer_ != 0) glDeleteFramebuffers(1, &framebuffer_);
    if (texture_ != 0) glDeleteTextures(1, &texture_);
    if (earth_texture_ != 0) glDeleteTextures(1, &earth_texture_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (grid_vao_ != 0) glDeleteVertexArrays(1, &grid_vao_);
    if (grid_vbo_ != 0) glDeleteBuffers(1, &grid_vbo_);
}

bool MiniMapRenderer::Initialize(uint32_t shader_program) {
    if (initialized_) return true;

    spdlog::info("[minimap] Initialize: shader_program={}", shader_program);

    if (shader_program == 0) {
        spdlog::error("[minimap] Initialize: invalid shader program provided");
        return false;
    }

    shader_program_ = shader_program;

    try {
        // Load Earth texture
        if (!LoadEarthTexture()) {
            spdlog::warn("Failed to load Earth texture, using fallback");
            GenerateFallbackGlobe();
        }

        // Create framebuffer and texture for mini-map
        glGenFramebuffers(1, &framebuffer_);
        glGenTextures(1, &texture_);

        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, config_.width, config_.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            spdlog::error("Mini-map framebuffer is not complete");
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Create geometry
        CreateGeometry();

        initialized_ = true;
        spdlog::info("Mini-map renderer initialized successfully");
        return true;

    } catch (const std::exception& e) {
        spdlog::error("Mini-map renderer initialization failed: {}", e.what());
        return false;
    }
}

void MiniMapRenderer::Update(CameraController* camera_controller,
                            [[maybe_unused]] uint32_t screen_width, [[maybe_unused]] uint32_t screen_height) {
    if (!initialized_ || !camera_controller) return;

    camera_controller_ = camera_controller;
    UpdateCameraPosition(camera_controller);
}

void MiniMapRenderer::Render(float aspect_ratio) {
    if (!initialized_) {
        spdlog::warn("[minimap] Render: not initialized");
        return;
    }



    // Save current viewport
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // Render to mini-map framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, config_.width, config_.height);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Black background
    glClear(GL_COLOR_BUFFER_BIT);

    RenderGlobe();
    if (config_.show_grid) {
        RenderGrid();
    }
    RenderCameraPosition();
    RenderFrustum(aspect_ratio);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Restore viewport
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);


}

bool MiniMapRenderer::LoadEarthTexture() {
    const char* texture_path = "/home/user/Downloads/03_Topo_small.png";

    int width, height, channels;
    unsigned char* data = stbi_load(texture_path, &width, &height, &channels, 4);
    if (!data) {
        spdlog::error("Failed to load Earth texture from: {}", texture_path);
        return false;
    }

    glGenTextures(1, &earth_texture_);
    glBindTexture(GL_TEXTURE_2D, earth_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    texture_loaded_ = true;
    spdlog::info("[minimap] Loaded Earth texture: {}x{} pixels, texture_id={}", width, height, earth_texture_);
    return true;
}

void MiniMapRenderer::GenerateFallbackGlobe() {
    // Generate a simple wireframe globe as fallback
    std::vector<unsigned char> data(config_.width * config_.height * 4, 0);

    // Draw simple latitude/longitude lines
    for (uint32_t y = 0; y < config_.height; ++y) {
        for (uint32_t x = 0; x < config_.width; ++x) {
            // Convert pixel to lat/lon
            float lon = (x * 360.0f / config_.width) - 180.0f;
            float lat = 90.0f - (y * 180.0f / config_.height);

            // Draw grid lines
            bool on_grid = (std::abs(lat - std::round(lat / 30.0f) * 30.0f) < 2.0f) ||
                          (std::abs(lon - std::round(lon / 30.0f) * 30.0f) < 2.0f);

            if (on_grid) {
                int idx = (y * config_.width + x) * 4;
                data[idx] = 64;     // R
                data[idx + 1] = 64; // G
                data[idx + 2] = 64; // B
                data[idx + 3] = 255; // A
            }
        }
    }

    glGenTextures(1, &earth_texture_);
    glBindTexture(GL_TEXTURE_2D, earth_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, config_.width, config_.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    texture_loaded_ = false;
    spdlog::info("[minimap] Generated fallback wireframe globe texture, texture_id={}", earth_texture_);
}

void MiniMapRenderer::CreateGeometry() {
    // Create quad for globe texture
    std::vector<float> vertices = {
        // positions        // texture coords
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  // top-left
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f,  // top-right
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  // bottom-right
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f   // bottom-left
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Texture coord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Create grid geometry
    std::vector<float> grid_vertices;
    const int num_lines = 12; // 6 latitude + 6 longitude lines

    for (int i = 0; i <= num_lines; ++i) {
        float angle = i * 360.0f / num_lines;
        // Longitude lines (vertical)
        float x = std::cos(glm::radians(angle));
        float z = std::sin(glm::radians(angle));
        grid_vertices.insert(grid_vertices.end(), {x, 0.0f, z, x, 1.0f, z});

        // Latitude lines (horizontal)
        float y = (i - num_lines/2.0f) / (num_lines/2.0f);
        grid_vertices.insert(grid_vertices.end(), {-1.0f, y, 0.0f, 1.0f, y, 0.0f});
    }

    glGenVertexArrays(1, &grid_vao_);
    glGenBuffers(1, &grid_vbo_);

    glBindVertexArray(grid_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo_);
    glBufferData(GL_ARRAY_BUFFER, grid_vertices.size() * sizeof(float), grid_vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
}

void MiniMapRenderer::UpdateCameraPosition(CameraController* camera_controller) {
    // Get camera position (normalized coordinates, Earth center at origin, radius 1)
    glm::vec3 pos = camera_controller->GetPosition();

    // Use CoordinateMapper for consistent geographic conversion (matches main globe)
    using namespace coordinates;
    World camera_world(pos);
    Geographic camera_geo = CoordinateMapper::WorldToGeographic(camera_world, constants::rendering::NORMALIZED_GLOBE_RADIUS);

    // Convert to pixel coordinates
    camera_position_pixels_ = LatLonToPixel(camera_geo.latitude, camera_geo.longitude);
}



void MiniMapRenderer::RenderGlobe() {


    glUseProgram(shader_program_);

    // Set up orthographic projection
    glm::mat4 projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uModel"), 1, GL_FALSE, glm::value_ptr(model));

    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, earth_texture_);
    glUniform1i(glGetUniformLocation(shader_program_, "uTexture"), 0);

    // Use texture with white tint
    glUniform4f(glGetUniformLocation(shader_program_, "uColor"), 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform1i(glGetUniformLocation(shader_program_, "uUseTexture"), 1);  // Enable texture sampling



    // Create temporary quad for full texture coverage
    std::vector<float> quad_vertices = {
        // positions        // tex coords (not used for debug red)
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,  // bottom-left
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  // bottom-right
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f,  // top-right
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f   // top-left
    };

    GLuint tempVAO, tempVBO;
    glGenVertexArrays(1, &tempVAO);
    glGenBuffers(1, &tempVBO);

    glBindVertexArray(tempVAO);
    glBindBuffer(GL_ARRAY_BUFFER, tempVBO);
    glBufferData(GL_ARRAY_BUFFER, quad_vertices.size() * sizeof(float), quad_vertices.data(), GL_STATIC_DRAW);

    // Position attribute (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Texture coord attribute (location 1) - not used for debug red
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Render quad covering entire texture
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Clean up
    glDeleteBuffers(1, &tempVBO);
    glDeleteVertexArrays(1, &tempVAO);


}

void MiniMapRenderer::RenderGrid() {
    glUseProgram(shader_program_);

    // Set grid color with opacity
    glUniform4f(glGetUniformLocation(shader_program_, "uColor"), 1.0f, 1.0f, 1.0f, config_.grid_opacity);
    glUniform1i(glGetUniformLocation(shader_program_, "uUseTexture"), 0);

    // Disable texture
    glBindTexture(GL_TEXTURE_2D, 0);

    // Render grid lines
    glBindVertexArray(grid_vao_);
    glDrawArrays(GL_LINES, 0, 24); // 12 lines * 2 vertices each
}

void MiniMapRenderer::RenderCameraPosition() {
    // Render red dot at camera position
    glUseProgram(shader_program_);

    // Set red color
    glUniform4f(glGetUniformLocation(shader_program_, "uColor"), 1.0f, 0.0f, 0.0f, 1.0f);
    glUniform1i(glGetUniformLocation(shader_program_, "uUseTexture"), 0);

    // Disable texture
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create small quad for camera position marker
    std::vector<float> marker_vertices = {
        -0.02f,  0.02f, 0.0f,
         0.02f,  0.02f, 0.0f,
         0.02f, -0.02f, 0.0f,
        -0.02f, -0.02f, 0.0f
    };

    // Position the marker
    glm::mat4 model = glm::translate(glm::mat4(1.0f),
        glm::vec3(2.0f * (camera_position_pixels_.x / config_.width) - 1.0f,
                  1.0f - 2.0f * (camera_position_pixels_.y / config_.height),
                  0.0f));

    glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uModel"), 1, GL_FALSE, glm::value_ptr(model));

    // Render marker
    GLuint temp_vao, temp_vbo;
    glGenVertexArrays(1, &temp_vao);
    glGenBuffers(1, &temp_vbo);

    glBindVertexArray(temp_vao);
    glBindBuffer(GL_ARRAY_BUFFER, temp_vbo);
    glBufferData(GL_ARRAY_BUFFER, marker_vertices.size() * sizeof(float), marker_vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDeleteBuffers(1, &temp_vbo);
    glDeleteVertexArrays(1, &temp_vao);
}

void MiniMapRenderer::RenderFrustum(float aspect_ratio) {
    if (!camera_controller_) return;

    // Get camera frustum
    Frustum frustum = camera_controller_->GetFrustum(aspect_ratio);
    float near_plane = camera_controller_->GetNearPlane();
    float far_plane = camera_controller_->GetFarPlane();

    // Get frustum corners
    auto corners = frustum.GetCorners(near_plane, far_plane);
    glm::vec3 camera_pos = camera_controller_->GetPosition();

    // Far plane corners are indices 4-7
    std::vector<glm::vec3> frustum_intersections;

    // For each far corner, compute ray intersection with Earth
    for (int i = 4; i < 8; ++i) {
        glm::vec3 corner = corners[i];
        glm::vec3 ray_dir = glm::normalize(corner - camera_pos);
        glm::vec3 intersection = RaySphereIntersection(camera_pos, ray_dir, glm::vec3(0.0f), 1.0f);

        if (glm::length(intersection) > 0.0f) {
            frustum_intersections.push_back(intersection);
        }
    }

    if (frustum_intersections.size() < 3) return; // Not enough points for rendering

    // Render trapezoid outline connecting frustum intersections
    if (frustum_intersections.size() >= 4) {
        glUseProgram(shader_program_);
        glUniform4f(glGetUniformLocation(shader_program_, "uColor"), 1.0f, 1.0f, 1.0f, 1.0f); // White lines
        glUniform1i(glGetUniformLocation(shader_program_, "uUseTexture"), 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Set up orthographic projection (same as globe)
        glm::mat4 projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));

        // Create trapezoid by connecting the 4 intersection points
        std::vector<float> trapezoid_vertices;
        for (size_t i = 0; i < frustum_intersections.size(); ++i) {
            glm::vec2 uv1 = WorldToUV(frustum_intersections[i]);
            glm::vec2 uv2 = WorldToUV(frustum_intersections[(i + 1) % frustum_intersections.size()]);

            // Convert UV to NDC (-1 to 1)
            float x1 = 2.0f * uv1.x - 1.0f;
            float y1 = 2.0f * uv1.y - 1.0f;
            float x2 = 2.0f * uv2.x - 1.0f;
            float y2 = 2.0f * uv2.y - 1.0f;

            trapezoid_vertices.insert(trapezoid_vertices.end(), {x1, y1, 0.0f, x2, y2, 0.0f});
        }

        // Render trapezoid outline
        GLuint temp_vao, temp_vbo;
        glGenVertexArrays(1, &temp_vao);
        glGenBuffers(1, &temp_vbo);

        glBindVertexArray(temp_vao);
        glBindBuffer(GL_ARRAY_BUFFER, temp_vbo);
        glBufferData(GL_ARRAY_BUFFER, trapezoid_vertices.size() * sizeof(float), trapezoid_vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);

        glDrawArrays(GL_LINES, 0, trapezoid_vertices.size() / 3);

        glDeleteBuffers(1, &temp_vbo);
        glDeleteVertexArrays(1, &temp_vao);
    }
}

glm::vec2 MiniMapRenderer::LatLonToPixel(float latitude, float longitude) const {
    // Convert lat/lon to equirectangular pixel coordinates
    float x = (longitude + 180.0f) * config_.width / 360.0f;
    // Note: this is not ok, since we are flipping, but at least not we vertically aligned
    float y = (90.0f - latitude) * config_.height / 180.0f;
    // float y = (latitude + 90.0f) * config_.height / 180.0f;

    // Clamp to valid range
    x = glm::clamp(x, 0.0f, static_cast<float>(config_.width));
    y = glm::clamp(y, 0.0f, static_cast<float>(config_.height));

    return glm::vec2(x, y);
}

void MiniMapRenderer::SetSize(uint32_t width, uint32_t height) {
    config_.width = width;
    config_.height = height;

    // Recreate framebuffer and texture
    if (initialized_) {
        glDeleteTextures(1, &texture_);
        glDeleteFramebuffers(1, &framebuffer_);

        glGenFramebuffers(1, &framebuffer_);
        glGenTextures(1, &texture_);

        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void MiniMapRenderer::SetOffset(uint32_t offset_x, uint32_t offset_y) {
    config_.offset_x = offset_x;
    config_.offset_y = offset_y;
}

} // namespace earth_map
