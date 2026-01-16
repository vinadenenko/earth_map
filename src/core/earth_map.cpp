#include <earth_map/earth_map.h>
#include <earth_map/core/earth_map_impl.h>
#include <earth_map/platform/library_info.h>
#include <memory>

namespace earth_map {

std::unique_ptr<EarthMap> EarthMap::Create(const Configuration& config) {
    try {
        return std::make_unique<EarthMapImpl>(config);
    } catch (const std::exception& e) {
        // Log error and return nullptr
        return nullptr;
    }
}

std::string LibraryInfo::GetVersion() {
    return std::string(EARTH_MAP_VERSION);
}

std::string LibraryInfo::GetBuildInfo() {
    return "Earth Map " + GetVersion() + " - Built on " __DATE__ " " __TIME__;
}

bool LibraryInfo::CheckSystemRequirements() {
    // Check OpenGL version
    // Check required extensions
    // Check system capabilities
    return true; // Placeholder
}

} // namespace earth_map