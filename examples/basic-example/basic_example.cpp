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
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/fmt/fmt.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
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

//==============================================================================
// Perf-test flight scenario (hotkey 'P')
//
// A fixed, reproducible camera flight used to compare PerformanceStats
// before/after a rendering fix -- run it, apply the fix, run it again, diff
// perf_flight.log. Input is locked out while it runs so two runs are
// actually comparable (no risk of a different mouse nudge explaining part
// of an observed difference).
//==============================================================================

// Mirrors tile_renderer.cpp's CalculateOptimalZoom() inverse
// (zoom = log2(K / altitude_normalized), K = (MIN_ALTITUDE_METERS /
// EARTH_MEAN_RADIUS) * 2^kMaxZoom). Solving for altitude in meters, the
// EARTH_MEAN_RADIUS terms cancel: altitude_meters = MIN_ALTITUDE_METERS *
// 2^(kMaxZoom - zoom). This is only used here to pick FlyTo() altitudes
// that reliably land on a specific tile zoom level for reproducible
// testing -- the renderer itself always derives zoom from live camera
// distance, never from this.
double AltitudeMetersForZoom(int zoom) {
    constexpr int kMaxZoom = 21;  // must match tile_renderer.cpp's kMaxZoom
    return earth_map::constants::camera_constraints::MIN_ALTITUDE_METERS *
           static_cast<double>(1ull << (kMaxZoom - zoom));
}

struct FlightWaypoint {
    double longitude;
    double latitude;
    int target_zoom;         // for logging only; altitude_meters is what's actually flown to
    double altitude_meters;
    float duration_seconds;  // flight time to reach this waypoint
};

// Alternates zoom dives with lateral movement across Armenia, to stress
// continuous new-tile streaming the same way real camera move/zoom does.
const std::vector<FlightWaypoint> kPerfFlightWaypoints = {
    {44.5152, 40.1872, 15, AltitudeMetersForZoom(15), 5.0f},  // Yerevan, close dive
    {44.5152, 40.1872, 8,  AltitudeMetersForZoom(8),  5.0f},  // pull out over the same spot
    {45.0116, 40.4000, 14, AltitudeMetersForZoom(14), 5.0f},  // Lake Sevan
    {43.8478, 40.7894, 15, AltitudeMetersForZoom(15), 5.0f},  // Gyumri, close dive
    {43.8478, 40.7894, 8,  AltitudeMetersForZoom(8),  5.0f},  // pull out over Gyumri
    {44.4939, 40.8123, 14, AltitudeMetersForZoom(14), 5.0f},  // Vanadzor
    {44.5453, 39.8814, 15, AltitudeMetersForZoom(15), 5.0f},  // Khor Virap, close dive
    {44.5152, 40.1872, 8,  AltitudeMetersForZoom(8),  5.0f},  // back out over Yerevan, end
};

static bool perf_scenario_active = false;
static std::size_t perf_scenario_waypoint_index = 0;
static float perf_scenario_waypoint_elapsed = 0.0f;
static float perf_scenario_total_elapsed = 0.0f;
static std::shared_ptr<spdlog::logger> perf_flight_logger;

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
    std::cout << "║   Ctrl + 2          : Jump to Yerevan zoom-13 test view    ║\n";
    std::cout << "║   O                 : Toggle debug overlay                 ║\n";
    std::cout << "║   P                 : Run/stop scripted perf-test flight    ║\n";
    std::cout << "║                       (logs PerformanceStats to             ║\n";
    std::cout << "║                       perf_flight.log; locks out input)     ║\n";
    std::cout << "║   H                 : Toggle this help text                ║\n";
    std::cout << "║   ESC               : Exit application                     ║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "║ Camera Modes:                                              ║\n";
    std::cout << "║   FREE   : Free-flying camera with WASD movement           ║\n";
    std::cout << "║   ORBIT  : Orbit around Earth center (no WASD)             ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
}

void StopPerfFlightScenario(const char* reason) {
    if (perf_flight_logger) {
        perf_flight_logger->info("=== perf flight scenario stopped: {} ===", reason);
        perf_flight_logger->flush();
    }
    perf_scenario_active = false;
    std::cout << "→ Perf flight scenario STOPPED (" << reason << ")\n";
}

void StartPerfFlightScenario(earth_map::CameraController* camera) {
    if (!camera || kPerfFlightWaypoints.empty()) {
        return;
    }

    perf_scenario_active = true;
    perf_scenario_waypoint_index = 0;
    perf_scenario_waypoint_elapsed = 0.0f;
    perf_scenario_total_elapsed = 0.0f;

    // Fresh logger each run so perf_flight.log is truncated, not appended --
    // each hotkey press is meant to be one clean, comparable run.
    spdlog::drop("perf_flight");
    perf_flight_logger = spdlog::basic_logger_mt("perf_flight", "perf_flight.log", /*truncate=*/true);
    perf_flight_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
    // Flush every line: this scenario exists to stress the exact code path
    // suspected of stalling/crashing, so an unflushed buffer could lose the
    // most important lines if the app doesn't exit cleanly.
    perf_flight_logger->flush_on(spdlog::level::info);

    const FlightWaypoint& first = kPerfFlightWaypoints[0];
    camera->FlyTo(first.longitude, first.latitude, first.altitude_meters, first.duration_seconds);

    perf_flight_logger->info("=== perf flight scenario started: {} waypoints ===",
                              kPerfFlightWaypoints.size());
    perf_flight_logger->info(
        "=== waypoint 1/{}: lon={:.4f} lat={:.4f} target_zoom={} altitude={:.0f}m duration={:.1f}s ===",
        kPerfFlightWaypoints.size(), first.longitude, first.latitude, first.target_zoom,
        first.altitude_meters, first.duration_seconds);

    std::cout << "→ Perf flight scenario STARTED -- logging to perf_flight.log, "
                 "input locked out until it finishes or P is pressed again\n";
}

// Called once per frame from the main loop. Advances the waypoint timeline
// and issues the next FlyTo() when the current leg's duration elapses.
void AdvancePerfFlightScenario(earth_map::CameraController* camera, float delta_time) {
    if (!perf_scenario_active || !camera) {
        return;
    }

    perf_scenario_waypoint_elapsed += delta_time;
    perf_scenario_total_elapsed += delta_time;

    const FlightWaypoint& current = kPerfFlightWaypoints[perf_scenario_waypoint_index];
    if (perf_scenario_waypoint_elapsed < current.duration_seconds) {
        return;
    }

    ++perf_scenario_waypoint_index;
    if (perf_scenario_waypoint_index >= kPerfFlightWaypoints.size()) {
        StopPerfFlightScenario("completed all waypoints");
        return;
    }

    perf_scenario_waypoint_elapsed = 0.0f;
    const FlightWaypoint& next = kPerfFlightWaypoints[perf_scenario_waypoint_index];
    camera->FlyTo(next.longitude, next.latitude, next.altitude_meters, next.duration_seconds);

    if (perf_flight_logger) {
        perf_flight_logger->info(
            "=== waypoint {}/{}: lon={:.4f} lat={:.4f} target_zoom={} altitude={:.0f}m duration={:.1f}s ===",
            perf_scenario_waypoint_index + 1, kPerfFlightWaypoints.size(), next.longitude,
            next.latitude, next.target_zoom, next.altitude_meters, next.duration_seconds);
    }
}

// Called once per frame from the main loop while the scenario is active.
// Logs the same PerformanceStats the console overlay shows, but every
// frame (not once/sec) so transient spikes are actually visible in the log.
void LogPerfFlightFrame(earth_map::EarthMap* earth_map_instance) {
    if (!perf_scenario_active || !perf_flight_logger || !earth_map_instance) {
        return;
    }
    auto* renderer = earth_map_instance->GetRenderer();
    if (!renderer) {
        return;
    }

    const earth_map::PerformanceStats stats = renderer->GetStats();

    std::string zones_str;
    for (const auto& zone : stats.zones) {
        zones_str += fmt::format(" | {}: cpu={:.3f}ms gpu={} draws={}", zone.name, zone.cpu_ms,
                                  zone.gpu_ms ? fmt::format("{:.3f}ms", *zone.gpu_ms)
                                              : std::string("n/a"),
                                  zone.draw_calls);
    }

    perf_flight_logger->info(
        "t={:.2f}s wp={}/{} fps={} cpu={:.2f}ms gpu={}{}", perf_scenario_total_elapsed,
        perf_scenario_waypoint_index + 1, kPerfFlightWaypoints.size(), stats.fps,
        stats.frame_cpu_ms,
        stats.frame_gpu_ms ? fmt::format("{:.3f}ms", *stats.frame_gpu_ms) : std::string("n/a"),
        zones_str);
}

// Callback function for window resize
void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

// Keyboard callback
void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
    if (!g_earth_map_instance) return;

    auto camera = g_earth_map_instance->GetCameraController();
    if (!camera) return;

    // While the scripted perf flight is running, ignore everything except
    // P (stop it early) and ESC (still allow quitting) -- keeps the two
    // runs being compared free of any incidental interactive input.
    if (perf_scenario_active && key != GLFW_KEY_P && key != GLFW_KEY_ESCAPE) {
        return;
    }

    // Handle key press events
    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_P: {
                if (perf_scenario_active) {
                    StopPerfFlightScenario("stopped early by user");
                } else {
                    StartPerfFlightScenario(camera);
                }
                break;
            }
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
            case GLFW_KEY_2: {
                if ((mods & GLFW_MOD_CONTROL) == 0) {
                    break;
                }
                // Reproducible high-zoom view: Yerevan at an altitude that
                // selects z13 with the renderer's current zoom calibration.
                camera->SetGeographicPosition(44.5152, 40.1872, 25000.0);
                camera->SetMovementMode(earth_map::CameraController::MovementMode::ORBIT);
                std::cout << "Tag. Inspect preset: Yerevan zoom-13 view\n";
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
    if (perf_scenario_active) {
        return;
    }
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
    if (perf_scenario_active) {
        return;
    }
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
    if (perf_scenario_active) {
        return;
    }
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
        
        // Initialize GLEW. glewExperimental is required here: this window
        // was created with GLFW_OPENGL_CORE_PROFILE above, and GLEW's
        // classic extension-string query (glGetString(GL_EXTENSIONS)) is
        // invalid on core-profile contexts -- glewExperimental switches it
        // to the glGetStringi-based query instead. See
        // earth_map::Renderer::Initialize()'s documented precondition
        // (renderer.h) for why this is this host's responsibility, not
        // earth_map's.
        glewExperimental = GL_TRUE;
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

#ifdef EARTH_MAP_EXAMPLE_CACERT_PATH
        // libcurl has no default CA bundle location on Windows, so HTTPS
        // tile requests fail peer verification there unless CURLOPT_CAINFO
        // is set explicitly (Linux finds the system store on its own).
        config.tile_loader_config.ca_cert_path = EARTH_MAP_EXAMPLE_CACERT_PATH;
#endif

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
            const earth_map::geodesy::EcefPosition pos = camera->GetEcefPosition();
            const earth_map::geodesy::EcefPosition target = camera->GetEcefTarget();
            auto orient = camera->GetOrientation();
            auto mode = camera->GetMovementMode();
            float fov = camera->GetFieldOfView();

            std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
            std::cout << "║          INITIAL CAMERA STATE                              ║\n";
            std::cout << "╠════════════════════════════════════════════════════════════╣\n";
            std::cout << "║ Position (ECEF m): (" << pos.meters.x << ", " << pos.meters.y << ", "
                      << pos.meters.z << ")\n";
            std::cout << "║ Target   (ECEF m): (" << target.meters.x << ", " << target.meters.y
                      << ", " << target.meters.z << ")\n";
            std::cout << "║ Distance from Earth's centre: " << glm::length(pos.meters) / 1000.0
                      << " km\n";
            std::cout << "║ Heading:   " << orient.x << "°\n";
            std::cout << "║ Pitch:     " << orient.y << "°\n";
            std::cout << "║ Roll:      " << orient.z << "°\n";
            std::cout << "║ FOV:       " << fov << "°\n";
            std::cout << "║ Mode:      " << (mode == earth_map::CameraController::MovementMode::FREE ? "FREE" : "ORBIT") << "\n";

            // Calculate view direction
            const glm::dvec3 view_dir = glm::normalize(target.meters - pos.meters);
            std::cout << "║ View direction: (" << view_dir.x << ", " << view_dir.y << ", "
                      << view_dir.z << ")\n";

            // Check if globe should be visible
            const double globe_radius = earth_map::constants::geodetic::EARTH_SEMI_MAJOR_AXIS;  // meters
            const double distance_to_origin = glm::length(pos.meters);
            const double nearest_globe_point = distance_to_origin - globe_radius;
            const double farthest_globe_point = distance_to_origin + globe_radius;

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
            AdvancePerfFlightScenario(camera, delta_time);
            if (camera) {
                camera->Update(delta_time);
            }

            // Render
            earth_map_instance->Render();
            LogPerfFlightFrame(earth_map_instance.get());

            // Swap buffers and poll events
            glfwSwapBuffers(window);
            glfwPollEvents();

            // Update frame counter
            frame_count++;

            // Print debug overlay every 1 second
            auto elapsed = std::chrono::duration<float>(current_time - last_overlay_time).count();
            if (show_overlay && elapsed >= 1.0f) {
                if (camera) {
                    const earth_map::geodesy::EcefPosition pos = camera->GetEcefPosition();
                    auto orient = camera->GetOrientation();
                    const earth_map::geodesy::EcefPosition target = camera->GetEcefTarget();
                    auto mode = camera->GetMovementMode();
                    float fps = frame_count / elapsed;

                    const double distance_from_origin = glm::length(pos.meters);
                    const double globe_radius = earth_map::constants::geodetic::EARTH_SEMI_MAJOR_AXIS;
                    const double distance_from_surface = distance_from_origin - globe_radius;

                    // Calculate view direction
                    const glm::dvec3 view_dir = glm::normalize(target.meters - pos.meters);

                    // Clear a few lines and print overlay
                    std::cout << "\r\033[K";  // Clear line
                    std::cout << "╔═══════════════════════════════════ DEBUG OVERLAY ═══════════════════════════════════╗\n";
                    std::cout << "║ FPS: " << static_cast<int>(fps) << " fps                                                                         ║\n";
                    std::cout << "║ Camera Position (ECEF): ("
                              << static_cast<int>(pos.meters.x/1000) << ", "
                              << static_cast<int>(pos.meters.y/1000) << ", "
                              << static_cast<int>(pos.meters.z/1000) << ") km                    ║\n";
                    std::cout << "║ Globe Center: (0, 0, 0) km                                                         ║\n";
                    std::cout << "║ Distance from origin: " << static_cast<int>(distance_from_origin/1000) << " km                                             ║\n";
                    std::cout << "║ Distance from surface: " << static_cast<int>(distance_from_surface/1000) << " km                                            ║\n";
                    std::cout << "║ View Direction: ("
                              << std::fixed << std::setprecision(2) << view_dir.x << ", "
                              << view_dir.y << ", " << view_dir.z << ")                                     ║\n";
                    std::cout << "║ Heading: " << static_cast<int>(orient.x) << "°  |  Pitch: " << static_cast<int>(orient.y) << "°  |  Roll: " << static_cast<int>(orient.z) << "°                                   ║\n";
                    std::cout << "║ Mode: " << (mode == earth_map::CameraController::MovementMode::FREE ? "FREE (WASD enabled)" : "ORBIT (WASD disabled)") << "                                                    ║\n";
                    std::cout << "╚═══════════════════════════════════════════════════════════════════════════════════╝\n";

                    // earth_map's own internal frame timing (fps/cpu are
                    // always live; gpu/zones are only populated when the
                    // library was built with
                    // EARTH_MAP_ENABLE_PERFORMANCE_MONITORING -- see
                    // include/earth_map/renderer/performance_stats.h).
                    if (auto* renderer = earth_map_instance->GetRenderer()) {
                        const earth_map::PerformanceStats stats = renderer->GetStats();
                        std::cout << "  [perf] fps=" << stats.fps
                                  << " cpu=" << std::fixed << std::setprecision(2) << stats.frame_cpu_ms << "ms"
                                  << " gpu=" << (stats.frame_gpu_ms ? std::to_string(*stats.frame_gpu_ms) + "ms" : "n/a")
                                  << "\n";
                        for (const auto& zone : stats.zones) {
                            std::cout << "    " << zone.name << ": " << zone.cpu_ms << "ms cpu / "
                                      << (zone.gpu_ms ? std::to_string(*zone.gpu_ms) + "ms" : "n/a") << " gpu"
                                      << " (" << zone.draw_calls << " draws)\n";
                        }
                    }
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
