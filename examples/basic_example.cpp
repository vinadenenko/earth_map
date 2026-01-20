// Include system headers first
#include "earth_map/renderer/renderer.h"
#include "earth_map/renderer/tile_renderer.h"
#include <iostream>
#include <exception>
#include <chrono>
#include <thread>

// Include GLEW before GLFW to avoid conflicts
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// Then include earth_map headers
#include <earth_map/earth_map.h>
#include <earth_map/core/camera_controller.h>
#include <earth_map/platform/library_info.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

// Global variables for mouse interaction
static double last_mouse_x = 0.0;
static double last_mouse_y = 0.0;
static bool mouse_dragging = false;
static earth_map::EarthMap* g_earth_map_instance = nullptr;
static bool show_help = true;

// Helper function to print camera controls
void print_help() {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║          EARTH MAP - CAMERA CONTROLS                       ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Mouse Controls:                                            ║\n";
    std::cout << "║   Left Mouse + Drag : Rotate camera view                   ║\n";
    std::cout << "║   Scroll Wheel      : Zoom in/out                          ║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "║ Keyboard Controls:                                         ║\n";
    std::cout << "║   F                 : Toggle camera mode (FREE/ORBIT)      ║\n";
    std::cout << "║   R                 : Reset camera to default view         ║\n";
    std::cout << "║   H                 : Toggle this help text                ║\n";
    std::cout << "║   ESC               : Exit application                     ║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "║ Camera Modes:                                              ║\n";
    std::cout << "║   FREE   : Free-flying camera, rotate around view point   ║\n";
    std::cout << "║   ORBIT  : Orbit around Earth center                       ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
}

// Callback function for window resize
void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

// Keyboard callback
void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action == GLFW_PRESS && g_earth_map_instance) {
        auto camera = g_earth_map_instance->GetCameraController();
        if (!camera) return;

        switch (key) {
            case GLFW_KEY_F: {
                // Toggle camera mode
                auto current_mode = camera->GetMovementMode();
                if (current_mode == earth_map::CameraController::MovementMode::FREE) {
                    camera->SetMovementMode(earth_map::CameraController::MovementMode::ORBIT);
                    std::cout << "→ Camera Mode: ORBIT (orbiting around Earth)\n";
                } else {
                    camera->SetMovementMode(earth_map::CameraController::MovementMode::FREE);
                    std::cout << "→ Camera Mode: FREE (free-flying)\n";
                }
                break;
            }
            case GLFW_KEY_R:
                // Reset camera
                camera->Reset();
                std::cout << "→ Camera reset to default view\n";
                break;
            case GLFW_KEY_H:
                // Toggle help
                show_help = !show_help;
                if (show_help) {
                    print_help();
                } else {
                    std::cout << "→ Help hidden (press H to show again)\n";
                }
                break;
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, true);
                break;
        }
    }
}

// Mouse button callback
void mouse_button_callback(GLFWwindow* window, int button, int action, int /*mods*/) {
    spdlog::info("Click");
    if (g_earth_map_instance) {
        auto camera = g_earth_map_instance->GetCameraController();
        if (camera) {
            // Update dragging state for our own tracking
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                if (action == GLFW_PRESS) {
                    glfwGetCursorPos(window, &last_mouse_x, &last_mouse_y);
                    mouse_dragging = true;
                    spdlog::debug("Mouse button pressed at ({:.1f}, {:.1f})", last_mouse_x, last_mouse_y);
                } else if (action == GLFW_RELEASE) {
                    mouse_dragging = false;
                    spdlog::debug("Mouse button released");
                }
            }
        }
    }
    
    // Suppress unused parameter warning for window
    (void)window;
}

// Mouse motion callback
void cursor_position_callback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (mouse_dragging && g_earth_map_instance) {
        double dx = xpos - last_mouse_x;
        double dy = ypos - last_mouse_y;
        
        auto camera = g_earth_map_instance->GetCameraController();
        if (camera) {
            // Calculate rotation based on mouse movement
            float rotation_speed = 0.5f; // degrees per pixel
            float heading_delta = static_cast<float>(dx) * rotation_speed;
            float pitch_delta = static_cast<float>(-dy) * rotation_speed; // Invert Y for natural feeling
            
            // Get current orientation
            auto current_orientation = camera->GetOrientation();
            
            // Update orientation
            float new_heading = current_orientation.x + heading_delta;
            float new_pitch = std::clamp(current_orientation.y + pitch_delta, -89.0f, 89.0f);
            
            camera->SetOrientation(new_heading, new_pitch, current_orientation.z);
            
            if (std::abs(dx) > 0.1 || std::abs(dy) > 0.1) {
                spdlog::debug("Mouse drag: dx={:.2f}, dy={:.2f}, heading_delta={:.2f}, pitch_delta={:.2f}", 
                            dx, dy, heading_delta, pitch_delta);
            }
            
            last_mouse_x = xpos;
            last_mouse_y = ypos;
        }
    }
}

// Scroll callback for zoom
void scroll_callback(GLFWwindow* /*window*/, double xoffset, double yoffset) {
    spdlog::info("Scroll");
    if (g_earth_map_instance) {
        auto camera = g_earth_map_instance->GetCameraController();
        if (camera) {
            // Simple zoom by adjusting altitude
            auto current_pos = camera->GetPosition();
            float distance = glm::length(current_pos);
            float zoom_factor = 1.0f + static_cast<float>(yoffset) * 0.1f;
            float new_distance = distance * zoom_factor;
            
            // Clamp to reasonable range
            new_distance = std::clamp(new_distance, 1000.0f, 10000000.0f);
            
            // Move camera along its forward vector
            glm::vec3 forward = glm::normalize(-current_pos);
            glm::vec3 new_pos = forward * new_distance;
            
            camera->SetPosition(new_pos);
            
            spdlog::info("Scroll: zoom_factor={:.3f}, new_distance={:.1f}", zoom_factor, new_distance);
            
            // TODO: Trigger tile loading at new zoom level
            // This would involve:
            // 1. Calculating new tile coordinates based on zoom level
            // 2. Requesting tiles from the tile loader  
            // 3. Updating texture manager with new tiles
            
            // Suppress unused parameter warning
            (void)xoffset;
        }
    }
}

int main() {
    try {
        std::cout << "Earth Map Basic Example\n";
        std::cout << "========================\n\n";
        
        // Display library info
        std::cout << "Library Version: " << earth_map::LibraryInfo::GetVersion() << "\n";
        std::cout << "Build Info: " << earth_map::LibraryInfo::GetBuildInfo() << "\n";

        // spdlog::set_level(spdlog::level::debug);
        
        // Initialize GLFW
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW\n";
            return -1;
        }
        
        // Configure GLFW
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        // glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Run in headless mode for debugging

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
        
        // Set input callbacks
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetKeyCallback(window, key_callback);
        glfwSetMouseButtonCallback(window, mouse_button_callback);
        glfwSetCursorPosCallback(window, cursor_position_callback);
        glfwSetScrollCallback(window, scroll_callback);
        
        // Initialize GLEW
        if (glewInit() != GLEW_OK) {
            std::cerr << "Failed to initialize GLEW\n";
            glfwDestroyWindow(window);
            glfwTerminate();
            return -1;
        }
        
        // Create Earth Map instance
        std::cout << "Creating Earth Map instance...\n";
        earth_map::Configuration config;
        config.screen_width = window_width;
        config.screen_height = window_height;
        config.enable_performance_monitoring = true;
        
        auto earth_map_instance = earth_map::EarthMap::Create(config);
        if (!earth_map_instance) {
            std::cerr << "Failed to create Earth Map instance\n";
            glfwDestroyWindow(window);
            glfwTerminate();
            return -1;
        }
        
        std::cout << "Earth Map instance created successfully\n";
        
        // Set global instance for callbacks
        g_earth_map_instance = earth_map_instance.get();
        
        // Initialize Earth Map with OpenGL context
        if (!earth_map_instance->Initialize()) {
            std::cerr << "Failed to initialize Earth Map\n";
            glfwDestroyWindow(window);
            glfwTerminate();
            return -1;
        }
        
        std::cout << "Earth Map initialized successfully\n";

        // Check system requirements now that OpenGL context is fully initialized
        std::cout << "System Requirements: "
                  << (earth_map::LibraryInfo::CheckSystemRequirements() ? "Met" : "Not Met")
                  << "\n\n";

        // Display help
        print_help();

        // Display initial camera state
        auto camera = earth_map_instance->GetCameraController();
        if (camera) {
            auto pos = camera->GetPosition();
            auto mode = camera->GetMovementMode();
            std::cout << "Initial Camera State:\n";
            std::cout << "  Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
            std::cout << "  Distance from origin: " << glm::length(pos) / 1000.0 << " km\n";
            std::cout << "  Mode: " << (mode == earth_map::CameraController::MovementMode::FREE ? "FREE" : "ORBIT") << "\n\n";
        }

        // Main render loop
        std::cout << "Starting render loop...\n\n";

        auto last_time = std::chrono::high_resolution_clock::now();
        int frame_count = 0;
        auto last_status_time = last_time;

        while (!glfwWindowShouldClose(window)) {
            // Calculate delta time
            auto current_time = std::chrono::high_resolution_clock::now();
            float delta_time = std::chrono::duration<float>(current_time - last_time).count();
            last_time = current_time;

            // Update camera
            auto camera = earth_map_instance->GetCameraController();
            if (camera) {
                camera->Update(delta_time);
            }

            // Render
            earth_map_instance->Render();

            // Swap buffers and poll events
            glfwSwapBuffers(window);
            glfwPollEvents();

            // Update frame counter
            frame_count++;

            // Print status every 5 seconds
            auto elapsed = std::chrono::duration<float>(current_time - last_status_time).count();
            if (elapsed >= 5.0f) {
                if (camera) {
                    auto pos = camera->GetPosition();
                    auto orient = camera->GetOrientation();
                    auto mode = camera->GetMovementMode();
                    float fps = frame_count / elapsed;

                    std::cout << "Status: FPS=" << static_cast<int>(fps)
                              << " | Distance=" << static_cast<int>(glm::length(pos) / 1000.0) << "km"
                              << " | Heading=" << static_cast<int>(orient.x) << "°"
                              << " | Pitch=" << static_cast<int>(orient.y) << "°"
                              << " | Mode=" << (mode == earth_map::CameraController::MovementMode::FREE ? "FREE" : "ORBIT")
                              << "\n";
                }
                frame_count = 0;
                last_status_time = current_time;
            }
        }
        
        std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
        std::cout << "║  Application shutting down...                              ║\n";
        std::cout << "╚════════════════════════════════════════════════════════════╝\n";

        // Cleanup
        earth_map_instance.reset();
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
