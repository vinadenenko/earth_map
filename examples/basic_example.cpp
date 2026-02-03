// Include system headers first
#include "earth_map/renderer/renderer.h"
#include "earth_map/renderer/tile_renderer.h"
#include <iostream>
#include <exception>
#include <chrono>
#include <thread>
#include <iomanip>

// Include GLEW before GLFW to avoid conflicts
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// Then include earth_map headers
#include <earth_map/earth_map.h>
#include <earth_map/constants.h>
#include <earth_map/core/camera_controller.h>
#include <earth_map/platform/library_info.h>
#include <earth_map/coordinates/coordinate_mapper.h>
#include <earth_map/coordinates/coordinate_spaces.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
#include <iomanip>

// Global variables for mouse interaction
static double last_mouse_x = 0.0;
static double last_mouse_y = 0.0;
static bool mouse_dragging = false;
static earth_map::EarthMap* g_earth_map_instance = nullptr;
static bool show_help = true;
static bool show_overlay = true;

// Double-click detection
static double last_click_time = 0.0;
static constexpr double DOUBLE_CLICK_THRESHOLD = 0.3; // seconds

// Movement state is now handled entirely by the library's Camera class.
// WASD key events are forwarded via ProcessInput() which sets internal
// movement impulses, and UpdateMovement() applies them with constraint
// enforcement. No external movement_state needed.

// Helper function to print camera controls
void print_help() {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║          EARTH MAP - CAMERA CONTROLS                       ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Mouse Controls:                                            ║\n";
    std::cout << "║   Left Mouse + Drag   : Rotate camera view                 ║\n";
    std::cout << "║   Middle Mouse + Drag : Tilt camera (pitch/heading)        ║\n";
    std::cout << "║   Double Click        : Zoom to clicked location           ║\n";
    std::cout << "║   Scroll Wheel        : Zoom in/out                        ║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "║ Keyboard Controls:                                         ║\n";
    std::cout << "║   W / S             : Move forward / backward (FREE mode)  ║\n";
    std::cout << "║   A / D             : Move left / right (FREE mode)        ║\n";
    std::cout << "║   Q / E             : Move up / down (FREE mode)           ║\n";
    std::cout << "║   F                 : Toggle camera mode (FREE/ORBIT)      ║\n";
    std::cout << "║   M                 : Toggle mini-map                       ║\n";
    std::cout << "║   R                 : Reset camera to default view         ║\n";
    std::cout << "║   1                 : Jump to Himalayas (SRTM data region) ║\n";
    std::cout << "║   O                 : Toggle debug overlay                 ║\n";
    std::cout << "║   H                 : Toggle this help text                ║\n";
    std::cout << "║   ESC               : Exit application                     ║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "║ Camera Modes:                                              ║\n";
    std::cout << "║   FREE   : Free-flying camera with WASD movement           ║\n";
    std::cout << "║   ORBIT  : Orbit around Earth center (no WASD)             ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
}

// Callback function for window resize
void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

// Keyboard callback
void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (!g_earth_map_instance) return;

    auto camera = g_earth_map_instance->GetCameraController();
    if (!camera) return;

    // Handle key press events
    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_F: {
                // Toggle camera mode
                auto current_mode = camera->GetMovementMode();
                if (current_mode == earth_map::CameraController::MovementMode::FREE) {
                    camera->SetMovementMode(earth_map::CameraController::MovementMode::ORBIT);
                    std::cout << "→ Camera Mode: ORBIT (orbiting around Earth)\n";
                } else {
                    camera->SetMovementMode(earth_map::CameraController::MovementMode::FREE);
                    std::cout << "→ Camera Mode: FREE (free-flying with WASD)\n";
                }
                break;
            }
            case GLFW_KEY_R:
                camera->Reset();
                std::cout << "→ Camera reset to default view\n";
                break;
            case GLFW_KEY_1: {
                // Jump to Himalayan region (where SRTM data is)
                // Coordinates: 27-29°N, 86-94°E (Mt. Everest region)
                camera->SetGeographicPosition(90.0, 28.0, 500000.0);  // 500km altitude
                camera->SetMovementMode(earth_map::CameraController::MovementMode::ORBIT);
                std::cout << "→ Jumped to Himalayan region (SRTM data area)\n";
                break;
            }
            case GLFW_KEY_O:
                show_overlay = !show_overlay;
                std::cout << "→ Debug overlay: " << (show_overlay ? "ON" : "OFF") << "\n";
                break;

            case GLFW_KEY_M: {
                bool enabled = g_earth_map_instance->IsMiniMapEnabled();
                g_earth_map_instance->EnableMiniMap(!enabled);
                std::cout << "→ Mini-map: " << (!enabled ? "ON" : "OFF") << "\n";
                break;
            }
            case GLFW_KEY_H:
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

            // Movement keys — forward to library via ProcessInput
            // The camera's HandleKeyPress/HandleKeyRelease set internal movement
            // impulses which UpdateMovement() applies with constraint enforcement.
            case GLFW_KEY_W:
            case GLFW_KEY_S:
            case GLFW_KEY_A:
            case GLFW_KEY_D:
            case GLFW_KEY_Q:
            case GLFW_KEY_E: {
                earth_map::InputEvent event;
                event.type = earth_map::InputEvent::Type::KEY_PRESS;
                event.key = key;
                camera->ProcessInput(event);
                break;
            }
        }
    }

    // Handle key release events — forward WASD to library
    if (action == GLFW_RELEASE) {
        switch (key) {
            case GLFW_KEY_W:
            case GLFW_KEY_S:
            case GLFW_KEY_A:
            case GLFW_KEY_D:
            case GLFW_KEY_Q:
            case GLFW_KEY_E: {
                earth_map::InputEvent event;
                event.type = earth_map::InputEvent::Type::KEY_RELEASE;
                event.key = key;
                camera->ProcessInput(event);
                break;
            }
        }
    }
}

// Mouse button callback
void mouse_button_callback(GLFWwindow* window, int button, int action, int /*mods*/) {
    if (g_earth_map_instance) {
        auto camera = g_earth_map_instance->GetCameraController();
        if (camera) {
            // Detect double-click on left mouse button
            if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
                double current_time = glfwGetTime();
                double time_since_last_click = current_time - last_click_time;

                // Get mouse position
                double mouse_x, mouse_y;
                glfwGetCursorPos(window, &mouse_x, &mouse_y);

                // Check for double-click
                if (time_since_last_click < DOUBLE_CLICK_THRESHOLD) {
                    // Double-click detected
                    earth_map::InputEvent double_click_event;
                    double_click_event.type = earth_map::InputEvent::Type::DOUBLE_CLICK;
                    double_click_event.button = button;
                    double_click_event.x = static_cast<float>(mouse_x);
                    double_click_event.y = static_cast<float>(mouse_y);
                    double_click_event.timestamp = current_time * 1000.0;

                    camera->ProcessInput(double_click_event);

                    std::cout << "→ Double-click detected: zooming to location\n";

                    // Reset click time to prevent triple-click
                    last_click_time = 0.0;
                    return;  // Don't process as regular click
                }

                last_click_time = current_time;
            }

            // Convert click to geographic coordinates on left mouse press
            if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
                // Use actual OpenGL viewport (handles retina/HiDPI correctly)
                GLint gl_viewport[4];
                glGetIntegerv(GL_VIEWPORT, gl_viewport);
                glm::ivec4 viewport(gl_viewport[0], gl_viewport[1], gl_viewport[2], gl_viewport[3]);

                // Get camera matrices
                float aspect_ratio = static_cast<float>(gl_viewport[2]) / gl_viewport[3];
                auto view_matrix = camera->GetViewMatrix();
                auto proj_matrix = camera->GetProjectionMatrix(aspect_ratio);

                // Get mouse position
                double mouse_x, mouse_y;
                glfwGetCursorPos(window, &mouse_x, &mouse_y);

                // Scale mouse coordinates for retina/HiDPI displays
                int window_width, window_height;
                glfwGetWindowSize(window, &window_width, &window_height);
                double scale_x = static_cast<double>(gl_viewport[2]) / window_width;
                double scale_y = static_cast<double>(gl_viewport[3]) / window_height;

                // Convert screen coordinates (flip Y for OpenGL: GLFW Y=0 at top, OpenGL Y=0 at bottom)
                earth_map::coordinates::Screen screen_point(
                    mouse_x * scale_x,
                    (window_height - mouse_y) * scale_y
                );
                auto geo_coords = earth_map::coordinates::CoordinateMapper::ScreenToGeographic(
                    screen_point, view_matrix, proj_matrix, viewport, 1.0f);

                // Note we have a very huge distorsion in latitude (to poles)
                if (geo_coords) {
                    std::cout << "Clicked location: Lat " << std::fixed << std::setprecision(4)
                              << geo_coords->latitude << "°, Lon " << geo_coords->longitude << "°" << std::endl;
                } else {
                    std::cout << "Click did not hit the globe" << std::endl;
                }
            }

            // Create InputEvent and forward to camera
            earth_map::InputEvent event;

            if (action == GLFW_PRESS) {
                event.type = earth_map::InputEvent::Type::MOUSE_BUTTON_PRESS;
                glfwGetCursorPos(window, &last_mouse_x, &last_mouse_y);
                mouse_dragging = true;
            } else if (action == GLFW_RELEASE) {
                event.type = earth_map::InputEvent::Type::MOUSE_BUTTON_RELEASE;
                mouse_dragging = false;
            }

            event.button = button;
            event.x = last_mouse_x;
            event.y = last_mouse_y;
            event.timestamp = glfwGetTime() * 1000.0;  // Convert to milliseconds

            camera->ProcessInput(event);
        }
    }
}

// Mouse motion callback
void cursor_position_callback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (g_earth_map_instance) {
        auto camera = g_earth_map_instance->GetCameraController();
        if (camera) {
            // Create InputEvent and forward to camera
            earth_map::InputEvent event;
            event.type = earth_map::InputEvent::Type::MOUSE_MOVE;
            event.x = xpos;
            event.y = ypos;
            event.timestamp = glfwGetTime() * 1000.0;  // Convert to milliseconds

            camera->ProcessInput(event);

            last_mouse_x = xpos;
            last_mouse_y = ypos;
        }
    }
}

// Scroll callback for zoom
void scroll_callback(GLFWwindow* /*window*/, double xoffset, double yoffset) {
    if (g_earth_map_instance) {
        auto camera = g_earth_map_instance->GetCameraController();
        if (camera) {
            // Create InputEvent and forward to camera
            earth_map::InputEvent event;
            event.type = earth_map::InputEvent::Type::MOUSE_SCROLL;
            event.scroll_delta = static_cast<float>(yoffset);
            event.timestamp = glfwGetTime() * 1000.0;  // Convert to milliseconds

            camera->ProcessInput(event);
        }
    }

    (void)xoffset;  // Suppress unused parameter warning
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

        // Example usage of custom XYZ tile provider
        auto googleProvider = std::make_shared<earth_map::BasicXYZTileProvider>(
            "GoogleMaps",
            "https://mt{s}.google.com/vt/lyrs=m&x={x}&y={y}&z={z}&key=YOUR_API_KEY",
            "0123",  // Subdomains for load balancing
            0,       // Min zoom
            21,      // Max zoom
            "png"    // Format
            );
        config.tile_provider = googleProvider;

        // Using SRTM data
        config.elevation_config.enabled = true;
        config.elevation_config.exaggeration_factor = 100.5f;  // Exaggerate for visibility
        config.srtm_loader_config.local_directory = "./srtm_data";

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

        // Debug: Check renderer state
        auto renderer = earth_map_instance->GetRenderer();
        if (renderer) {
            auto stats = renderer->GetStats();
            std::cout << "Renderer Stats:\n";
            std::cout << "  Draw calls: " << stats.draw_calls << "\n";
            std::cout << "  Triangles: " << stats.triangles_rendered << "\n";
            std::cout << "  Vertices: " << stats.vertices_processed << "\n";
        }

        // Debug: Check OpenGL state
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        std::cout << "OpenGL Viewport: " << viewport[0] << ", " << viewport[1] << ", "
                  << viewport[2] << ", " << viewport[3] << "\n";

        GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cull_face = glIsEnabled(GL_CULL_FACE);
        std::cout << "Depth Test: " << (depth_test ? "ENABLED" : "DISABLED") << "\n";
        std::cout << "Cull Face: " << (cull_face ? "ENABLED" : "DISABLED") << "\n";

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
            auto orient = camera->GetOrientation();
            auto target = camera->GetTarget();
            auto mode = camera->GetMovementMode();
            float fov = camera->GetFieldOfView();

            std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
            std::cout << "║          INITIAL CAMERA STATE                              ║\n";
            std::cout << "╠════════════════════════════════════════════════════════════╣\n";
            std::cout << "║ Position:  (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
            std::cout << "║ Target:    (" << target.x << ", " << target.y << ", " << target.z << ")\n";
            std::cout << "║ Distance from origin: " << glm::length(pos) / 1000.0 << " km\n";
            std::cout << "║ Heading:   " << orient.x << "°\n";
            std::cout << "║ Pitch:     " << orient.y << "°\n";
            std::cout << "║ Roll:      " << orient.z << "°\n";
            std::cout << "║ FOV:       " << fov << "°\n";
            std::cout << "║ Mode:      " << (mode == earth_map::CameraController::MovementMode::FREE ? "FREE" : "ORBIT") << "\n";

            // Calculate view direction
            glm::vec3 view_dir = glm::normalize(target - pos);
            std::cout << "║ View direction: (" << view_dir.x << ", " << view_dir.y << ", " << view_dir.z << ")\n";

            // Check if globe should be visible
            float globe_radius = static_cast<float>(earth_map::constants::geodetic::EARTH_SEMI_MAJOR_AXIS);  // meters
            float distance_to_origin = glm::length(pos);
            float nearest_globe_point = distance_to_origin - globe_radius;
            float farthest_globe_point = distance_to_origin + globe_radius;

            std::cout << "║\n";
            std::cout << "║ Globe radius: " << globe_radius / 1000.0 << " km\n";
            std::cout << "║ Nearest globe point: " << nearest_globe_point / 1000.0 << " km from camera\n";
            std::cout << "║ Farthest globe point: " << farthest_globe_point / 1000.0 << " km from camera\n";
            std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
        }

        // Main render loop
        std::cout << "Starting render loop...\n\n";

        auto last_time = std::chrono::high_resolution_clock::now();
        int frame_count = 0;
        auto last_overlay_time = last_time;

        while (!glfwWindowShouldClose(window)) {
            // Calculate delta time
            auto current_time = std::chrono::high_resolution_clock::now();
            float delta_time = std::chrono::duration<float>(current_time - last_time).count();
            // const float max_delta_time = 0.1f;  // Cap the delta_time to avoid extreme movement
            // delta_time = glm::min(delta_time, max_delta_time);
            last_time = current_time;

            // Update camera — all movement is handled internally by the library
            // via ProcessInput() key events and UpdateMovement() with constraint
            // enforcement. No manual position manipulation needed.
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

            // Print debug overlay every 1 second
            auto elapsed = std::chrono::duration<float>(current_time - last_overlay_time).count();
            if (show_overlay && elapsed >= 1.0f) {
                if (camera) {
                    auto pos = camera->GetPosition();
                    auto orient = camera->GetOrientation();
                    auto target = camera->GetTarget();
                    auto mode = camera->GetMovementMode();
                    float fps = frame_count / elapsed;

                    float distance_from_origin = glm::length(pos);
                    float globe_radius = static_cast<float>(earth_map::constants::geodetic::EARTH_SEMI_MAJOR_AXIS);
                    float distance_from_surface = distance_from_origin - globe_radius;

                    // Calculate view direction
                    glm::vec3 view_dir = glm::normalize(target - pos);

                    // Clear a few lines and print overlay
                    std::cout << "\r\033[K";  // Clear line
                    std::cout << "╔═══════════════════════════════════ DEBUG OVERLAY ═══════════════════════════════════╗\n";
                    std::cout << "║ FPS: " << static_cast<int>(fps) << " fps                                                                         ║\n";
                    std::cout << "║ Camera Position: ("
                              << static_cast<int>(pos.x/1000) << ", "
                              << static_cast<int>(pos.y/1000) << ", "
                              << static_cast<int>(pos.z/1000) << ") km                    ║\n";
                    std::cout << "║ Globe Center: (0, 0, 0) km                                                         ║\n";
                    std::cout << "║ Distance from origin: " << static_cast<int>(distance_from_origin/1000) << " km                                             ║\n";
                    std::cout << "║ Distance from surface: " << static_cast<int>(distance_from_surface/1000) << " km                                            ║\n";
                    std::cout << "║ View Direction: ("
                              << std::fixed << std::setprecision(2) << view_dir.x << ", "
                              << view_dir.y << ", " << view_dir.z << ")                                     ║\n";
                    std::cout << "║ Heading: " << static_cast<int>(orient.x) << "°  |  Pitch: " << static_cast<int>(orient.y) << "°  |  Roll: " << static_cast<int>(orient.z) << "°                                   ║\n";
                    std::cout << "║ Mode: " << (mode == earth_map::CameraController::MovementMode::FREE ? "FREE (WASD enabled)" : "ORBIT (WASD disabled)") << "                                                    ║\n";
                    std::cout << "╚═══════════════════════════════════════════════════════════════════════════════════╝\n";
                    std::cout << std::flush;
                }
                frame_count = 0;
                last_overlay_time = current_time;
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
