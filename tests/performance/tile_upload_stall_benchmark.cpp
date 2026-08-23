/**
 * @file tile_upload_stall_benchmark.cpp
 * @brief Isolates and measures the tile.upload GPU stall documented in
 * dev_docs/solved-dev-issues/issue-01-tile-upload-gpu-stall.md.
 *
 * Reproduces the exact hazard in isolation: a draw call samples earth_map's
 * shared GL_TEXTURE_2D_ARRAY, then a new tile is uploaded into a layer of
 * that same array via TileTexturePool::UploadTile(). Run this before and
 * after a fix and compare the reported GPU time directly -- this is a
 * comparison tool, not a pass/fail gate (real GPU timing is inherently
 * hardware/driver-dependent).
 *
 * Needs a real, current OpenGL context: mirrors
 * examples/basic-example/basic_example.cpp's GLFW window/context setup
 * exactly (same profile request, same glewExperimental requirement -- see
 * that file, or Renderer::Initialize()'s documented precondition in
 * renderer.h, for why this is necessary).
 */

#include <benchmark/benchmark.h>

#include <earth_map/imagery/tile_matrix_set.h>
#include <earth_map/renderer/tile_pool/tile_texture_pool.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>

namespace {

GLFWwindow* CreateHeadlessGlContext() {
    if (!glfwInit()) {
        std::cerr << "tile_upload_stall_benchmark: glfwInit() failed\n";
        std::exit(1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // Deliberately visible, unlike a typical "headless" benchmark: on this
    // machine's software (swrast) GL driver, an invisible/off-screen window
    // has no real backing drawable and glewInit()'s internal queries
    // corrupt driver state against it, segfaulting on the next GL call
    // (verified via gdb backtrace, crash inside swrast_dri.so). A real,
    // visible window -- same as basic_example.cpp actually uses -- gives
    // the driver a real surface and avoids this entirely.

    GLFWwindow* window = glfwCreateWindow(64, 64, "earth_map_benchmarks", nullptr, nullptr);
    if (!window) {
        std::cerr << "tile_upload_stall_benchmark: glfwCreateWindow() failed\n";
        glfwTerminate();
        std::exit(1);
    }
    glfwMakeContextCurrent(window);

    // Core profile requested above means GLEW's classic extension-string
    // query is invalid without this -- see Renderer::Initialize()'s
    // documented precondition (renderer.h): this is the host's job, and
    // this benchmark is its own host, same as basic_example/qt-test-app.
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "tile_upload_stall_benchmark: glewInit() failed\n";
        std::exit(1);
    }
    // glewInit() itself raises a benign GL_INVALID_ENUM on some core-profile
    // drivers (a well-known GLEW quirk) -- discard it so it isn't mistaken
    // for a real error by anything checking glGetError() afterward.
    while (glGetError() != GL_NO_ERROR) {
    }

    GLint max_layers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
    std::cerr << "tile_upload_stall_benchmark: GL_MAX_ARRAY_TEXTURE_LAYERS=" << max_layers
              << ", GL_VENDOR=" << glGetString(GL_VENDOR)
              << ", GL_RENDERER=" << glGetString(GL_RENDERER) << "\n";

    return window;
}

// Minimal geometry + shader that samples the same sampler2DArray the real
// tile renderer samples -- just enough GPU work to create the same
// read/write hazard against TileTexturePool's texture array, nothing more.
class SamplingDraw {
public:
    SamplingDraw() {
        static const char* kVertexSrc = R"(#version 330 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";
        static const char* kFragmentSrc = R"(#version 330 core
uniform sampler2DArray uTiles;
out vec4 fragColor;
void main() { fragColor = texture(uTiles, vec3(0.5, 0.5, 0.0)); }
)";
        program_ = CompileProgram(kVertexSrc, kFragmentSrc);

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        glUseProgram(program_);
        glUniform1i(glGetUniformLocation(program_, "uTiles"), 0);
        glUseProgram(0);
    }

    ~SamplingDraw() {
        glDeleteProgram(program_);
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
    }

    SamplingDraw(const SamplingDraw&) = delete;
    SamplingDraw& operator=(const SamplingDraw&) = delete;

    void Draw(std::uint32_t texture_array_id) const {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture_array_id);
        glUseProgram(program_);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
    }

private:
    static GLuint CompileShader(GLenum type, const char* src) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "tile_upload_stall_benchmark: shader compile failed: " << log << "\n";
            std::exit(1);
        }
        return shader;
    }

    static GLuint CompileProgram(const char* vertex_src, const char* fragment_src) {
        const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertex_src);
        const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragment_src);
        const GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::cerr << "tile_upload_stall_benchmark: program link failed: " << log << "\n";
            std::exit(1);
        }
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return program;
    }

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};

// Blocking GL_TIME_ELAPSED query -- deliberately synchronous. This is the
// opposite tradeoff from the live renderer's double-buffered
// GpuElapsedTimeQuery (which must never stall a real frame): a benchmark's
// whole job is one precise, isolated per-iteration number, so blocking for
// it here is correct, not a bug.
double TimeGpuWork(const std::function<void()>& work) {
    GLuint query = 0;
    glGenQueries(1, &query);
    glBeginQuery(GL_TIME_ELAPSED, query);
    work();
    glEndQuery(GL_TIME_ELAPSED);

    GLuint64 elapsed_ns = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed_ns);  // blocks until available
    glDeleteQueries(1, &query);
    return static_cast<double>(elapsed_ns) / 1e9;
}

earth_map::imagery::ImageTileKey MakeKey(int index) {
    earth_map::imagery::ImageTileKey key;
    key.imagery_source_id = "benchmark";
    key.matrix_set_id = "benchmark";
    key.address = {0, static_cast<std::uint32_t>(index), 0};
    return key;
}

constexpr std::uint32_t kTileSize = 256;
// Matches TileTextureCoordinator's real kDefaultMaxPoolLayers (128MB
// array) deliberately, not a small synthetic pool: if the hazard is a
// whole-array driver-side copy-to-avoid-stalling, its cost should scale
// with array size, and a tiny pool would understate the real effect.
constexpr std::uint32_t kLayers = 8;

}  // namespace

// Reproduces issue-01: upload a new tile into the shared texture array
// immediately after a draw call samples that same array. Before the fix,
// this should show gpu time far above BM_TileUpload_WithoutContention's
// baseline; after, the two should converge.
static void BM_TileUpload_WhileSampled(benchmark::State& state) {
    GLFWwindow* window = CreateHeadlessGlContext();
    {
        earth_map::TileTexturePool pool(kTileSize, kLayers, /*skip_gl_init=*/false);
        const SamplingDraw sampler;
        const std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(kTileSize) * kTileSize * 4, 0x7F);

        int next_key = 0;
        for (auto _ : state) {
            sampler.Draw(pool.GetTextureArrayID());

            const auto key = MakeKey(next_key);
            next_key = (next_key + 1) % static_cast<int>(kLayers);

            const double gpu_seconds = TimeGpuWork([&] {
                pool.UploadTile(key, pixels.data(), kTileSize, kTileSize, 4);
            });
            state.SetIterationTime(gpu_seconds);
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
}
BENCHMARK(BM_TileUpload_WhileSampled)->UseManualTime()->Iterations(50)->Unit(benchmark::kMillisecond);

// Control: the same upload, with no preceding draw call sampling the
// array. If this reports dramatically lower GPU time than the benchmark
// above, that gap IS the read/write hazard from issue-01 -- not some
// inherent cost of glTexSubImage3D itself.
static void BM_TileUpload_WithoutContention(benchmark::State& state) {
    GLFWwindow* window = CreateHeadlessGlContext();
    {
        earth_map::TileTexturePool pool(kTileSize, kLayers, /*skip_gl_init=*/false);
        const std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(kTileSize) * kTileSize * 4, 0x7F);

        int next_key = 0;
        for (auto _ : state) {
            const auto key = MakeKey(next_key);
            next_key = (next_key + 1) % static_cast<int>(kLayers);

            const double gpu_seconds = TimeGpuWork([&] {
                pool.UploadTile(key, pixels.data(), kTileSize, kTileSize, 4);
            });
            state.SetIterationTime(gpu_seconds);
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
}
BENCHMARK(BM_TileUpload_WithoutContention)->UseManualTime()->Iterations(50)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
