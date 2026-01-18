#include <earth_map/renderer/renderer.h>
#include <earth_map/earth_map.h>
#include <earth_map/platform/opengl_context.h>
#include <earth_map/renderer/tile_renderer.h>
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

uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform vec3 uViewPos;
uniform vec3 uObjectColor;

void main() {
    // Ambient lighting
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * uLightColor;
    
    // Diffuse lighting
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;
    
    // Specular lighting
    float specularStrength = 0.5;
    vec3 viewDir = normalize(uViewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * uLightColor;
    
    vec3 result = (ambient + diffuse + specular) * uObjectColor;
    FragColor = vec4(result, 1.0);
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
            
            CreateGlobeMesh();
            SetupOpenGLState();
            
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
                                                 glm::vec3(0.0f), frustum);
            tile_renderer_->RenderTiles(view_matrix, projection_matrix);
            tile_renderer_->EndFrame();
        }
        
        // Fallback to basic globe if tile renderer not available
        if (!tile_renderer_ || tile_renderer_->GetStats().visible_tiles == 0) {
            glUseProgram(shader_program_);
            
            // Set uniforms
            glm::mat4 model = glm::mat4(1.0f);
            
            glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uModel"), 
                              1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uView"), 
                              1, GL_FALSE, glm::value_ptr(view_matrix));
            glUniformMatrix4fv(glGetUniformLocation(shader_program_, "uProjection"), 
                              1, GL_FALSE, glm::value_ptr(projection_matrix));
            
            glUniform3f(glGetUniformLocation(shader_program_, "uLightPos"), 2.0f, 2.0f, 2.0f);
            glUniform3f(glGetUniformLocation(shader_program_, "uLightColor"), 1.0f, 1.0f, 1.0f);
            glUniform3f(glGetUniformLocation(shader_program_, "uViewPos"), 0.0f, 0.0f, 3.0f);
            glUniform3f(glGetUniformLocation(shader_program_, "uObjectColor"), 0.2f, 0.6f, 0.2f);
            
            // Render globe
            glBindVertexArray(vao_);
            glDrawElements(GL_TRIANGLES, globe_indices_.size(), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }
    
    void BeginFrame() override {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    }
    
    void EndFrame() override {
        // Update stats (simplified)
        stats_.draw_calls = 1;
        stats_.triangles_rendered = globe_indices_.size() / 3;
        stats_.vertices_processed = globe_vertices_.size();
    }
    
    void Render() override {
        if (!initialized_) {
            return;
        }
        
        BeginFrame();
        
        // Simple view and projection matrices for demo
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 
                                              static_cast<float>(config_.screen_width) / config_.screen_height, 
                                              0.1f, 100.0f);
        
        Frustum frustum; // Empty frustum for now
        RenderScene(view, projection, frustum);
        
        EndFrame();
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
    
    // Globe geometry
    std::vector<float> globe_vertices_;
    std::vector<unsigned int> globe_indices_;
    
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
    
    void CreateGlobeMesh() {
        const float radius = 1.0f;
        const int segments = 32;
        const int rings = 16;
        
        // Generate vertices
        for (int r = 0; r <= rings; ++r) {
            float theta = static_cast<float>(r) * glm::pi<float>() / rings;
            float sin_theta = std::sin(theta);
            float cos_theta = std::cos(theta);
            
            for (int s = 0; s <= segments; ++s) {
                float phi = static_cast<float>(s) * 2.0f * glm::pi<float>() / segments;
                float sin_phi = std::sin(phi);
                float cos_phi = std::cos(phi);
                
                // Position
                float x = radius * sin_theta * cos_phi;
                float y = radius * cos_theta;
                float z = radius * sin_theta * sin_phi;
                
                // Normal (same as position for sphere)
                float nx = sin_theta * cos_phi;
                float ny = cos_theta;
                float nz = sin_theta * sin_phi;
                
                // Texture coordinates
                float u = static_cast<float>(s) / segments;
                float v = static_cast<float>(r) / rings;
                
                // Add vertex (position + normal + texcoord)
                globe_vertices_.insert(globe_vertices_.end(), {x, y, z, nx, ny, nz, u, v});
            }
        }
        
        // Generate indices
        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < segments; ++s) {
                int current = r * (segments + 1) + s;
                int next = current + segments + 1;
                
                // First triangle
                globe_indices_.insert(globe_indices_.end(), {
                    static_cast<unsigned int>(current), 
                    static_cast<unsigned int>(next), 
                    static_cast<unsigned int>(current + 1)
                });
                // Second triangle
                globe_indices_.insert(globe_indices_.end(), {
                    static_cast<unsigned int>(next), 
                    static_cast<unsigned int>(next + 1), 
                    static_cast<unsigned int>(current + 1)
                });
            }
        }
    }
    
    void SetupOpenGLState() {
        // Create VAO, VBO, EBO
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);
        
        // Bind VAO
        glBindVertexArray(vao_);
        
        // Bind and fill VBO
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, globe_vertices_.size() * sizeof(float), 
                    globe_vertices_.data(), GL_STATIC_DRAW);
        
        // Bind and fill EBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, globe_indices_.size() * sizeof(unsigned int), 
                    globe_indices_.data(), GL_STATIC_DRAW);
        
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
        
        // Enable depth testing
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        
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
