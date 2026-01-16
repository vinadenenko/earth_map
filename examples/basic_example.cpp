#include <earth_map/earth_map.h>
#include <iostream>
#include <exception>

int main() {
    try {
        std::cout << "Earth Map Basic Example\n";
        std::cout << "========================\n\n";
        
        // Display library info
        std::cout << "Library Version: " << earth_map::LibraryInfo::GetVersion() << "\n";
        std::cout << "Build Info: " << earth_map::LibraryInfo::GetBuildInfo() << "\n";
        std::cout << "System Requirements: " 
                  << (earth_map::LibraryInfo::CheckSystemRequirements() ? "Met" : "Not Met") 
                  << "\n\n";
        
        // Create Earth Map instance
        std::cout << "Creating Earth Map instance...\n";
        earth_map::Configuration config;
        config.screen_width = 1280;
        config.screen_height = 720;
        config.enable_performance_monitoring = true;
        
        auto earth_map = earth_map::EarthMap::Create(config);
        if (!earth_map) {
            std::cerr << "Failed to create Earth Map instance\n";
            return -1;
        }
        
        std::cout << "Earth Map instance created successfully\n";
        
        // Note: Without an OpenGL context, initialization will fail
        // In a real application, you would set up OpenGL context first
        std::cout << "Skipping OpenGL initialization (no context available in this example)\n";
        
        // Display performance stats (will be empty before initialization)
        std::cout << "\nPerformance Stats: " << earth_map->GetPerformanceStats() << "\n";
        
        std::cout << "\nExample completed successfully!\n";
        std::cout << "In a real application, you would:\n";
        std::cout << "1. Set up an OpenGL context\n";
        std::cout << "2. Initialize the Earth Map instance\n";
        std::cout << "3. Load data files (KML, KMZ, GeoJSON)\n";
        std::cout << "4. Run the render loop\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return -1;
    } catch (...) {
        std::cerr << "Unknown exception occurred\n";
        return -1;
    }
}