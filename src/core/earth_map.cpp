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

} // namespace earth_map
