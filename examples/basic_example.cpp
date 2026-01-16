#include <earth_map/earth_map.h>
#include <iostream>
#include <exception>
#include <GLFW/glfw3.h>
#include <GL/glew.h>
#include <chrono>
#include <thread>

// Callback function for window resize
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

int main() {
    try {
        std::cout << "Earth Map Basic Example\n";
        std::cout << "========================\n\n";
        
        // Display library info
        std::cout << "Library Version: " << earth_map::LibraryInfo::GetVersion() << "\n";
        std::cout << "Build Info: " << earth_map::LibraryInfo::GetBuildInfo() << "\n";
        
        // Initialize GLFW
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW\n";
            return -1;
        }
        
        // Configure GLFW
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        
        // Create window
        const int window_width = 1280;
        const int window_height = 720;
        GLFWwindow* window = glfwCreateWindow(window_width, window_height, "Earth Map - 3D Globe", NULL, NULL);
        if (!window) {
            std::cerr << "Failed to create GLFW window\n";
            glfwTerminate();
            return -1;
        }
        
        // Make the window's context current
        glfwMakeContextCurrent(window);
        
        // Set resize callback
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        
        // Initialize GLEW
        if (glewInit() != GLEW_OK) {
            std::cerr << "Failed to initialize GLEW\n";
            glfwDestroyWindow(window);
            glfwTerminate();
            return -1;
        }
        
        // Check system requirements
        std::cout << "System Requirements: " 
                  << (earth_map::LibraryInfo::CheckSystemRequirements() ? "Met" : "Not Met") 
                  << "\n\n";
        
        // Create Earth Map instance
        std::cout << "Creating Earth Map instance...\n";
        earth_map::Configuration config;
        config.screen_width = window_width;
        config.screen_height = window_height;
        config.enable_performance_monitoring = true;
        
        auto earth_map = earth_map::EarthMap::Create(config);
        if (!earth_map) {
            std::cerr << "Failed to create Earth Map instance\n";
            glfwDestroyWindow(window);
            glfwTerminate();
            return -1;
        }
        
        std::cout << "Earth Map instance created successfully\n";
        
        // Initialize Earth Map with OpenGL context
        if (!earth_map->Initialize()) {
            std::cerr << "Failed to initialize Earth Map\n";
            glfwDestroyWindow(window);
            glfwTerminate();
            return -1;
        }
        
        std::cout << "Earth Map initialized successfully\n";
        
        // Main render loop
        std::cout << "Starting render loop...\n";
        std::cout << "Press ESC to exit\n\n";
        
        auto last_time = std::chrono::high_resolution_clock::now();
        int frame_count = 0;
        
        while (!glfwWindowShouldClose(window)) {
            // Calculate delta time
            auto current_time = std::chrono::high_resolution_clock::now();
            float delta_time = std::chrono::duration<float>(current_time - last_time).count();
            last_time = current_time;
            
            // Process input
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(window, true);
            }
            
            // Update camera
            auto* camera = earth_map->GetCameraController();
            if (camera) {
                camera->Update(delta_time);
            }
            
            // Render
            earth_map->Render();
            
            // Swap buffers and poll events
            glfwSwapBuffers(window);
            glfwPollEvents();
            
            // Update frame counter
            frame_count++;
            
            // Update performance stats every second
            if (frame_count % 60 == 0) {
                std::string stats = earth_map->GetPerformanceStats();
                std::cout << "Performance: " << stats << "\n";
            }
        }
        
        std::cout << "\nRender loop ended\n";
        std::cout << "Total frames rendered: " << frame_count << "\n";
        
        // Cleanup
        earth_map.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        
        std::cout << "\nExample completed successfully!\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return -1;
    } catch (...) {
        std::cerr << "Unknown exception occurred\n";
        return -1;
    }
}