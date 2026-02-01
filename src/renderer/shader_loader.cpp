/**
 * @file shader_loader.cpp
 * @brief GLSL shader compilation and linking implementation
 */

#include <earth_map/renderer/shader_loader.h>
#include <spdlog/spdlog.h>
#include <GL/glew.h>
#include <array>

namespace earth_map {

std::uint32_t ShaderLoader::CompileShader(std::uint32_t type,
                                          const char* source,
                                          const std::string& shader_name) {
    const std::uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    std::int32_t success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        std::array<char, 1024> info_log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(info_log.size()), nullptr, info_log.data());
        spdlog::error("{} compilation failed: {}", shader_name, info_log.data());
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

std::uint32_t ShaderLoader::CreateProgram(const char* vertex_source,
                                          const char* fragment_source,
                                          const std::string& program_name) {
    const std::uint32_t vertex_shader = CompileShader(
        GL_VERTEX_SHADER, vertex_source, program_name + " vertex");
    if (vertex_shader == 0) {
        return 0;
    }

    const std::uint32_t fragment_shader = CompileShader(
        GL_FRAGMENT_SHADER, fragment_source, program_name + " fragment");
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return 0;
    }

    const std::uint32_t program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    // Shaders can be deleted after linking
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    std::int32_t success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        std::array<char, 1024> info_log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(info_log.size()), nullptr, info_log.data());
        spdlog::error("{} program linking failed: {}", program_name, info_log.data());
        glDeleteProgram(program);
        return 0;
    }

    spdlog::info("{} shader program linked successfully", program_name);
    return program;
}

} // namespace earth_map
