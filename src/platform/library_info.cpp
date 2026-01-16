#include <earth_map/platform/library_info.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <GL/glew.h>

namespace earth_map {

std::string LibraryInfo::GetVersion() {
    return "0.1.0";
}

std::string LibraryInfo::GetBuildInfo() {
    std::ostringstream oss;
    oss << "Earth Map " << GetVersion() << " - Built on " << __DATE__ << " " << __TIME__;
    return oss.str();
}

bool LibraryInfo::CheckSystemRequirements() {
    // Check if we can get basic OpenGL info
    const GLubyte* version = glGetString(GL_VERSION);
    if (!version) {
        spdlog::error("Could not get OpenGL version");
        return false;
    }
    
    std::string version_str(reinterpret_cast<const char*>(version));
    spdlog::info("OpenGL Version: {}", version_str);
    
    // Check for OpenGL 3.3 or higher
    if (version_str.find("3.3") == std::string::npos && 
        version_str.find("4.") == std::string::npos &&
        version_str.find("5.") == std::string::npos) {
        spdlog::warn("OpenGL version may be below required 3.3");
        return false;
    }
    
    // Check required extensions
    const GLubyte* extensions = glGetString(GL_EXTENSIONS);
    if (!extensions) {
        spdlog::warn("Could not get OpenGL extensions list");
    }
    
    spdlog::info("System requirements check passed");
    return true;
}

} // namespace earth_map