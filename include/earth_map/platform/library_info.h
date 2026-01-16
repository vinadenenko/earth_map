#pragma once

/**
 * @file library_info.h
 * @brief Library information and utilities
 * 
 * Provides version information, build details, and system requirement checks.
 */

#include <string>

namespace earth_map {

/**
 * @brief Library information and utilities
 */
class LibraryInfo {
public:
    /**
     * @brief Get library version
     * 
     * @return std::string Version string in format "major.minor.patch"
     */
    static std::string GetVersion();
    
    /**
     * @brief Get build information
     * 
     * @return std::string Build timestamp and configuration
     */
    static std::string GetBuildInfo();
    
    /**
     * @brief Check if system supports required OpenGL features
     * 
     * @return true if system meets requirements, false otherwise
     */
    static bool CheckSystemRequirements();
};

} // namespace earth_map