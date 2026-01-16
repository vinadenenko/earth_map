#pragma once

/**
 * @file opengl_context.h
 * @brief OpenGL context abstraction and utilities
 * 
 * Provides cross-platform OpenGL context management and extension loading.
 * Abstracts platform differences for OpenGL initialization.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <functional>

// Forward declaration for OpenGL types
typedef void* GLContext;  // Simplified for this interface

namespace earth_map {

/**
 * @brief OpenGL context configuration
 */
struct OpenGLConfig {
    /** OpenGL version (major.minor) */
    std::uint32_t version_major = 3;
    std::uint32_t version_minor = 3;
    
    /** Core profile vs compatibility profile */
    bool core_profile = true;
    
    /** Enable debug context */
    bool debug_context = false;
    
    /** Enable forward compatibility */
    bool forward_compatible = false;
    
    /** Multisample anti-aliasing samples */
    std::uint32_t msaa_samples = 0;
    
    /** Color buffer bits */
    std::uint32_t color_bits = 24;
    
    /** Depth buffer bits */
    std::uint32_t depth_bits = 24;
    
    /** Stencil buffer bits */
    std::uint32_t stencil_bits = 8;
    
    /** Enable sRGB framebuffer */
    bool srgb_capable = true;
};

/**
 * @brief OpenGL function callback type for error handling
 */
using OpenGLErrorCallback = std::function<void(std::uint32_t error, const std::string& description)>;

/**
 * @brief OpenGL context abstraction
 * 
 * Provides cross-platform OpenGL context management and extension loading.
 * This interface allows for different backends (GLFW, SDL, EGL, etc.).
 */
class OpenGLContext {
public:
    /**
     * @brief Create a new OpenGL context
     * 
     * @param config OpenGL configuration
     * @return std::unique_ptr<OpenGLContext> New context instance
     */
    static std::unique_ptr<OpenGLContext> Create(const OpenGLConfig& config = OpenGLConfig{});
    
    /**
     * @brief Virtual destructor
     */
    virtual ~OpenGLContext() = default;
    
    /**
     * @brief Initialize the OpenGL context
     * 
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool Initialize() = 0;
    
    /**
     * @brief Make this context current on the calling thread
     * 
     * @return true if context was made current, false otherwise
     */
    virtual bool MakeCurrent() = 0;
    
    /**
     * @brief Swap buffers (present the rendered frame)
     * 
     * @return true if buffers were swapped successfully, false otherwise
     */
    virtual bool SwapBuffers() = 0;
    
    /**
     * @brief Check if the context is valid and ready for use
     * 
     * @return true if context is valid, false otherwise
     */
    virtual bool IsValid() const = 0;
    
    /**
     * @brief Get the actual OpenGL version
     * 
     * @return std::pair<std::uint32_t, std::uint32_t> Major and minor version
     */
    virtual std::pair<std::uint32_t, std::uint32_t> GetActualVersion() const = 0;
    
    /**
     * @brief Check if a specific OpenGL extension is supported
     * 
     * @param extension_name Name of the extension to check
     * @return true if extension is supported, false otherwise
     */
    virtual bool IsExtensionSupported(const std::string& extension_name) const = 0;
    
    /**
     * @brief Set error callback for OpenGL errors
     * 
     * @param callback Function to call on OpenGL errors
     */
    virtual void SetErrorCallback(OpenGLErrorCallback callback) = 0;
    
    /**
     * @brief Enable or disable vertical sync
     * 
     * @param enabled true to enable VSync, false to disable
     * @return true if VSync state was set successfully, false otherwise
     */
    virtual bool SetVSyncEnabled(bool enabled) = 0;
    
    /**
     * @brief Get context information
     * 
     * @return std::string Formatted context information
     */
    virtual std::string GetContextInfo() const = 0;
    
    /**
     * @brief Get native handle to the context
     * 
     * @return void* Platform-specific handle
     */
    virtual void* GetNativeHandle() const = 0;

protected:
    /**
     * @brief Protected constructor to enforce factory pattern
     */
    OpenGLContext() = default;
};

/**
 * @brief OpenGL utility functions
 */
class OpenGLUtils {
public:
    /**
     * @brief Check for OpenGL errors
     * 
     * @return std::uint32_t OpenGL error code (0 = no error)
     */
    static std::uint32_t CheckError();
    
    /**
     * @brief Get string representation of OpenGL error
     * 
     * @param error OpenGL error code
     * @return std::string Human-readable error description
     */
    static std::string GetErrorString(std::uint32_t error);
    
    /**
     * @brief Get OpenGL vendor string
     * 
     * @return std::string GPU vendor
     */
    static std::string GetVendor();
    
    /**
     * @brief Get OpenGL renderer string
     * 
     * @return std::string GPU/renderer description
     */
    static std::string GetRenderer();
    
    /**
     * @brief Get OpenGL version string
     * 
     * @return std::string OpenGL version
     */
    static std::string GetVersion();
    
    /**
     * @brief Get GLSL version string
     * 
     * @return std::string GLSL version
     */
    static std::string GetGLSLVersion();
    
    /**
     * @brief Get maximum texture size
     * 
     * @return std::uint32_t Maximum texture dimension
     */
    static std::uint32_t GetMaxTextureSize();
    
    /**
     * @brief Get maximum render buffer size
     * 
     * @return std::uint32_t Maximum render buffer dimension
     */
    static std::uint32_t GetMaxRenderBufferSize();
    
    /**
     * @brief Get maximum number of vertex attributes
     * 
     * @return std::uint32_t Maximum vertex attributes
     */
    static std::uint32_t GetMaxVertexAttributes();
    
    /**
     * @brief Check if the system supports required OpenGL features
     * 
     * @return true if all required features are supported, false otherwise
     */
    static bool CheckRequiredFeatures();
    
    /**
     * @brief Enable OpenGL debug output (if available)
     * 
     * @param callback Callback function for debug messages
     * @return true if debug output was enabled successfully, false otherwise
     */
    static bool EnableDebugOutput(OpenGLErrorCallback callback);
};

} // namespace earth_map