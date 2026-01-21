#include <earth_map/renderer/renderer.h>
#include <earth_map/earth_map.h>
#include <earth_map/constants.h>
#include <earth_map/platform/opengl_context.h>
#include <earth_map/renderer/tile_renderer.h>
#include <earth_map/renderer/globe_mesh.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <vector>
#include <array>

namespace earth_map {

// Basic vertex and fragment shaders for a simple globe
const char* BASIC_VERTEX_SHADER = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

void main() {
    FragPos = vec3(uModel * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(uModel))) * aNormal;
    TexCoord = aTexCoord;
    
    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
)";

const char* BASIC_FRAGMENT_SHADER = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform vec3 uObjectColor;

void main() {
    FragColor = vec4(uObjectColor, 1.0);
}
)";

/**
 * @brief Basic renderer implementation
 */
class RendererImpl : public Renderer {
public:
    explicit RendererImpl(const Configuration& config) : config_(config) {
        spdlog::info("Creating renderer with resolution {}x{}", 
                    config.screen_width, config.screen_height);
    }
    
    ~RendererImpl() override {
        Cleanup();
    }
    
    bool Initialize() override {
        if (initialized_) {
            return true;
        }
        
        spdlog::info("Initializing renderer");
        
        try {
            // Initialize GLEW
            if (glewInit() != GLEW_OK) {
                spdlog::error("Failed to initialize GLEW");
                return false;
            }
            
            // Check OpenGL version
            const GLubyte* version = glGetString(GL_VERSION);
            spdlog::info("OpenGL Version: {}", reinterpret_cast<const char*>(version));
            
            if (!LoadShaders()) {
                spdlog::error("Failed to load shaders");
                return false;
            }

            // Create icosahedron globe mesh with normalized radius
            GlobeMeshParams params;
            params.radius = static_cast<double>(constants::rendering::NORMALIZED_GLOBE_RADIUS);
            params.max_subdivision_level = constants::rendering::DEFAULT_GLOBE_SUBDIVISION;
            params.enable_adaptive = false;  // Start simple, can enable later
            params.quality = MeshQuality::HIGH;
            params.enable_crack_prevention = true;

            globe_mesh_ = GlobeMesh::Create(params);
            if (!globe_mesh_ || !globe_mesh_->Generate()) {
                spdlog::error("Failed to create or generate globe mesh");
                return false;
            }

            spdlog::info("Globe mesh generated with {} vertices and {} triangles",
                globe_mesh_->GetVertices().size(),
                globe_mesh_->GetTriangles().size());

            SetupOpenGLState();

            // Store expected counts for corruption detection
            expected_globe_vertex_count_ = globe_mesh_->GetVertices().size();
            expected_globe_index_count_ = globe_mesh_->GetVertexIndices().size();
            spdlog::info("BASELINE: Stored expected mesh counts - vertices: {}, indices: {} (triangles: {})",
                expected_globe_vertex_count_, expected_globe_index_count_, expected_globe_index_count_ / 3);

        // Set initial viewport
        glViewport(0, 0, config_.screen_width, config_.screen_height);
        spdlog::info("Viewport set to {}x{}", config_.screen_width, config_.screen_height);

    // Initialize tile renderer
        tile_renderer_ = TileRenderer::Create();
        if (!tile_renderer_ || !tile_renderer_->Initialize()) {
            spdlog::error("Failed to create or initialize tile renderer");
            return false;
        }
        
        // Set tile manager for tile renderer (will be available through scene manager)
        // This is simplified - in a full system, tile manager would be passed from higher level
            
            initialized_ = true;
            spdlog::info("Renderer initialized successfully");
            return true;
            
        } catch (const std::exception& e) {
            spdlog::error("Exception during renderer initialization: {}", e.what());
            return false;
        }
    }
    
    void RenderScene(const glm::mat4& view_matrix,
                    const glm::mat4& projection_matrix,
                    const Frustum& frustum) override {
        if (!initialized_) {
            return;
        }
        
        // Update tile renderer with current view
        if (tile_renderer_) {
            tile_renderer_->BeginFrame();
            tile_renderer_->UpdateVisibleTiles(view_matrix, projection_matrix,
                                                 camera_controller_->GetPosition(), frustum);
            tile_renderer_->RenderTiles(view_matrix, projection_matrix);
            tile_renderer_->EndFrame();
        }
        
        // Fallback to basic globe if tile renderer not available
        if (!tile_renderer_ || tile_renderer_->GetStats().visible_tiles == 0) {
            // Log why we're using fallback globe rendering
            static bool logged_fallback_reason = false;
            if (!logged_fallback_reason) {
                if (!tile_renderer_) {
                    spdlog::info("Using fallback globe rendering: tile_renderer not available");
                } else {
                    spdlog::info("Using fallback globe rendering: visible_tiles = 0");
                }
                logged_fallback_reason = true;
            }

            // Log tile count periodically
            static int tile_log_count = 0;
            if (tile_renderer_ && ++tile_log_count % 60 == 0) {
                spdlog::debug("Visible tiles: {}", tile_renderer_->GetStats().visible_tiles);
            }

            glUseProgram(shader_program_);

            // Set uniforms
            glm::mat4 model = glm::mat4(1.0f);

            glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uModel"),
                              1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uView"),
                              1, GL_FALSE, glm::value_ptr(view_matrix));
            glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uProjection"),
                              1, GL_FALSE, glm::value_ptr(projection_matrix));

            glUniform3f(glGetUniformLocation(shader_program_, "uObjectColor"), 0.2f, 0.6f, 0.2f);

            // Render globe
            if (globe_mesh_) {
                // === CORRUPTION DETECTION LOGGING ===
                // Get current mesh state
                std::size_t current_vertex_count = globe_mesh_->GetVertices().size();
                std::size_t current_index_count = globe_mesh_->GetVertexIndices().size();

                // Check for corruption
                bool vertex_corrupted = (current_vertex_count != expected_globe_vertex_count_);
                bool index_corrupted = (current_index_count != expected_globe_index_count_);

                if (vertex_corrupted || index_corrupted) {
                    spdlog::critical("=== MESH CORRUPTION DETECTED ===");
                    spdlog::critical("Expected vertices: {}, Current: {} [{}]",
                        expected_globe_vertex_count_, current_vertex_count,
                        vertex_corrupted ? "CORRUPTED" : "OK");
                    spdlog::critical("Expected indices: {}, Current: {} [{}]",
                        expected_globe_index_count_, current_index_count,
                        index_corrupted ? "CORRUPTED" : "OK");
                    spdlog::critical("Expected triangles: {}, Current: {}",
                        expected_globe_index_count_ / 3, current_index_count / 3);

                    // Log camera state
                    if (camera_controller_) {
                        glm::vec3 cam_pos = camera_controller_->GetPosition();
                        spdlog::critical("Camera position: ({:.3f}, {:.3f}, {:.3f}), distance: {:.3f}",
                            cam_pos.x, cam_pos.y, cam_pos.z, glm::length(cam_pos));
                    }
                } else {
                    // Log periodically even when not corrupted (every 60 frames)
                    static int frame_count = 0;
                    if (++frame_count % 60 == 0) {
                        spdlog::info("Globe mesh OK: {} vertices, {} indices ({} triangles)",
                            current_vertex_count, current_index_count, current_index_count / 3);
                    }
                }

                // Check OpenGL state
                GLint current_vao = 0;
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current_vao);
                if (static_cast<GLuint>(current_vao) != 0) {
                    spdlog::warn("VAO already bound before globe rendering: {}", current_vao);
                }

                glBindVertexArray(vao_);

                // Verify VAO and EBO are correctly bound
                GLint bound_vao = 0, bound_ebo = 0;
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &bound_vao);
                glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound_ebo);

                if (static_cast<GLuint>(bound_vao) != vao_ || static_cast<GLuint>(bound_ebo) != ebo_) {
                    spdlog::error("=== OPENGL STATE CORRUPTION ===");
                    spdlog::error("Expected VAO: {}, Actual: {}", vao_, bound_vao);
                    spdlog::error("Expected EBO: {}, Actual: {}", ebo_, bound_ebo);
                }

                // Draw with current count (will show corruption visually)
                glDrawElements(GL_TRIANGLES, current_index_count, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            }
        }
    }

    
    void BeginFrame() override {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    }
    
    void EndFrame() override {
        // Update stats (simplified)
        stats_.draw_calls = 1;
        if (globe_mesh_) {
            stats_.triangles_rendered = globe_mesh_->GetVertexIndices().size() / 3;
            stats_.vertices_processed = globe_mesh_->GetVertices().size();
        }
    }
    
    void Render() override {
        if (!initialized_) {
            return;
        }

        spdlog::debug("Renderer::Render() - BeginFrame");
        BeginFrame();

        // Get view and projection matrices from camera controller
        glm::mat4 view;
        glm::mat4 projection;
        Frustum frustum;

        if (camera_controller_) {
            float aspect_ratio = static_cast<float>(config_.screen_width) / config_.screen_height;
            view = camera_controller_->GetViewMatrix();
            projection = camera_controller_->GetProjectionMatrix(aspect_ratio);
            frustum = camera_controller_->GetFrustum(aspect_ratio);
        } else {
            // Fallback to simple view and projection if no camera
            view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f));
            projection = glm::perspective(glm::radians(constants::camera::DEFAULT_FOV),
                                          static_cast<float>(config_.screen_width) / config_.screen_height,
                                          0.1f, 100.0f);
            frustum = Frustum(projection * view);
        }

        spdlog::debug("Renderer::Render() - RenderScene");
        RenderScene(view, projection, frustum);

        spdlog::debug("Renderer::Render() - EndFrame");
        EndFrame();
        spdlog::debug("Renderer::Render() - complete");
    }
    
    void Resize(std::uint32_t width, std::uint32_t height) override {
        config_.screen_width = width;
        config_.screen_height = height;
        glViewport(0, 0, width, height);
        spdlog::debug("Renderer resized to {}x{}", width, height);
    }
    
    RenderStats GetStats() const override {
        return stats_;
    }
    
    ShaderManager* GetShaderManager() override {
        return shader_manager_.get();
    }
    
    CameraController* GetCameraController() const override {
        return camera_controller_;
    }
    
    void SetCameraController(CameraController* camera_controller) override {
        camera_controller_ = camera_controller;
        
        // Pass camera controller to tile renderer
        if (tile_renderer_) {
            // Note: This is a simplified approach
            // In a full system, this would be handled through proper dependency injection
            // TODO
            spdlog::debug("Camera controller set in renderer");
        }
    }
    
    TileRenderer* GetTileRenderer() override {
        return tile_renderer_.get();
    }
    
    PlacemarkRenderer* GetPlacemarkRenderer() override {
        return nullptr; // TODO: Implement
    }
    
    LODManager* GetLODManager() override {
        return nullptr; // TODO: Implement
    }
    
    GPUResourceManager* GetGPUResourceManager() override {
        return nullptr; // TODO: Implement
    }

private:
    Configuration config_;
    bool initialized_ = false;
    RenderStats stats_;
    
    // OpenGL objects
    std::uint32_t shader_program_ = 0;
    std::uint32_t vao_ = 0;
    std::uint32_t vbo_ = 0;
    std::uint32_t ebo_ = 0;
    
    // Globe mesh (icosahedron-based)
    std::unique_ptr<GlobeMesh> globe_mesh_;

    // Expected mesh counts for corruption detection
    std::size_t expected_globe_vertex_count_ = 0;
    std::size_t expected_globe_index_count_ = 0;

    std::unique_ptr<ShaderManager> shader_manager_;
    std::unique_ptr<TileRenderer> tile_renderer_;
    CameraController* camera_controller_ = nullptr;
    
    bool LoadShaders() {
        // Compile vertex shader
        std::uint32_t vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex_shader, 1, &BASIC_VERTEX_SHADER, nullptr);
        glCompileShader(vertex_shader);
        
        if (!CheckShaderCompile(vertex_shader)) {
            return false;
        }
        
        // Compile fragment shader
        std::uint32_t fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment_shader, 1, &BASIC_FRAGMENT_SHADER, nullptr);
        glCompileShader(fragment_shader);
        
        if (!CheckShaderCompile(fragment_shader)) {
            return false;
        }
        
        // Link shader program
        shader_program_ = glCreateProgram();
        glAttachShader(shader_program_, vertex_shader);
        glAttachShader(shader_program_, fragment_shader);
        glLinkProgram(shader_program_);
        
        if (!CheckProgramLink(shader_program_)) {
            return false;
        }
        
        // Clean up shaders
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        
        return true;
    }
    
    bool CheckShaderCompile(std::uint32_t shader) {
        int success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char info_log[512];
            glGetShaderInfoLog(shader, 512, nullptr, info_log);
            spdlog::error("Shader compilation failed: {}", info_log);
            return false;
        }
        return true;
    }
    
    bool CheckProgramLink(std::uint32_t program) {
        int success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char info_log[512];
            glGetProgramInfoLog(program, 512, nullptr, info_log);
            spdlog::error("Shader program linking failed: {}", info_log);
            return false;
        }
        return true;
    }


    void SetupOpenGLState() {
        if (!globe_mesh_) {
            spdlog::error("Globe mesh not initialized");
            return;
        }

        // Convert GlobeVertex data to OpenGL format
        const auto& vertices = globe_mesh_->GetVertices();
        const auto& indices = globe_mesh_->GetVertexIndices();

        // Flatten vertex data: position(3) + normal(3) + texcoord(2) = 8 floats per vertex
        std::vector<float> vertex_data;
        vertex_data.reserve(vertices.size() * 8);

        for (const auto& vertex : vertices) {
            // Position
            vertex_data.push_back(vertex.position.x);
            vertex_data.push_back(vertex.position.y);
            vertex_data.push_back(vertex.position.z);
            // Normal
            vertex_data.push_back(vertex.normal.x);
            vertex_data.push_back(vertex.normal.y);
            vertex_data.push_back(vertex.normal.z);
            // Texcoord
            vertex_data.push_back(vertex.texcoord.x);
            vertex_data.push_back(vertex.texcoord.y);
        }

        // Create VAO, VBO, EBO
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        // Bind VAO
        glBindVertexArray(vao_);

        // Bind and fill VBO
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(float),
                    vertex_data.data(), GL_STATIC_DRAW);

        // Bind and fill EBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(std::uint32_t),
                    indices.data(), GL_STATIC_DRAW);

        // Set vertex attributes
        // Position (location = 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // Normal (location = 1)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // Texture coordinates (location = 2)
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);

        // Unbind VAO
        glBindVertexArray(0);

        spdlog::info("Globe mesh uploaded to GPU: {} vertices, {} indices",
            vertices.size(), indices.size());

        // Enable depth testing
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // Enable backface culling for better performance
        // Icosahedron topology is now correct with consistent CCW winding
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);  // Counter-clockwise winding is front face
        spdlog::info("Backface culling enabled (CCW winding)");

        // Set polygon mode
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    
    void Cleanup() {
        if (vao_) {
            glDeleteVertexArrays(1, &vao_);
            vao_ = 0;
        }
        if (vbo_) {
            glDeleteBuffers(1, &vbo_);
            vbo_ = 0;
        }
        if (ebo_) {
            glDeleteBuffers(1, &ebo_);
            ebo_ = 0;
        }
        if (shader_program_) {
            glDeleteProgram(shader_program_);
            shader_program_ = 0;
        }
    }
};

// Factory function
std::unique_ptr<Renderer> Renderer::Create(const Configuration& config) {
    return std::make_unique<RendererImpl>(config);
}

} // namespace earth_map
