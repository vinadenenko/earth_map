#pragma once

/**
 * @file shader_loader.h
 * @brief GLSL shader compilation and linking utilities
 *
 * Provides RAII-based shader program management with compile-time
 * embedded shader sources or runtime string literals.
 */

#include <cstdint>
#include <string>

/**
 * GLSL version/precision preamble, prepended to every embedded shader
 * source via adjacent string-literal concatenation (e.g.
 * `EARTH_MAP_GLSL_PREAMBLE R"(...)"`). Desktop GLSL and GLSL ES differ only
 * here for the shaders in this codebase -- everything after the preamble
 * is identical on both platforms. GLSL ES additionally requires an
 * explicit default float/int precision in fragment shaders (desktop GLSL
 * has no such requirement); declaring it in vertex shaders too is
 * harmless.
 */
#ifdef __ANDROID__
#define EARTH_MAP_GLSL_PREAMBLE \
    "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
#define EARTH_MAP_GLSL_PREAMBLE "#version 330 core\n"
#endif

namespace earth_map {

/**
 * @brief Compile and link GLSL shaders into an OpenGL program
 *
 * Handles shader compilation, error reporting, and cleanup.
 * Returns 0 on failure.
 */
class ShaderLoader {
public:
    /**
     * @brief Compile vertex and fragment shaders and link into a program
     *
     * @param vertex_source GLSL vertex shader source code
     * @param fragment_source GLSL fragment shader source code
     * @param program_name Human-readable name for error messages
     * @return OpenGL program ID, or 0 on failure
     */
    static std::uint32_t CreateProgram(const char* vertex_source,
                                       const char* fragment_source,
                                       const std::string& program_name = "shader");

private:
    static std::uint32_t CompileShader(std::uint32_t type,
                                       const char* source,
                                       const std::string& shader_name);
};

} // namespace earth_map
