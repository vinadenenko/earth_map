# Earth Map 3D Tile Renderer

#[![CI](https://github.com/.../badge.svg)](https://github.com/earth-map/earth_map/actions)
#[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
#[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)

Earth Map is a high-performance OpenGL-based 3D tile map renderer designed for GIS applications, inspired by Google Earth and NASA World Wind. This library specializes in rendering millions of placemarks (points, linestrings, polygons) on mobile devices with performance as the primary priority.

## 🚀 Features

- **Performance First**: Optimized for rendering millions of placemarks at 60+ FPS
- **Cross-Platform**: Support for Android, Linux, Windows, and macOS
- **GIS Comprehensive**: Support for KML, KMZ, GeoJSON formats with full mapping tools
- **Modern OpenGL**: Shader-based rendering with OpenGL ES 3.0+ compatibility
- **Flexible Threading**: Single-threaded foundation with multi-threading extension points
- **Mobile Optimized**: Aggressive LOD and culling for mobile GPUs

## 📋 Requirements

### System Requirements
- **Minimum**: OpenGL 3.3 / OpenGL ES 3.0
- **Recommended**: OpenGL 4.0 / OpenGL ES 3.2+
- **Compiler**: C++20 compatible (GCC 10+, Clang 12+, MSVC 2019+)
- **Platform**: Android 5.0+, Linux, Windows 10+, macOS 10.15+

### Dependencies
- **GLFW** - Window management and context creation
- **GLEW** - OpenGL extension loading
- **GLM** - Mathematics library for 3D graphics
- **nlohmann/json** - JSON parsing for configuration
- **pugixml** - XML parsing for KML support
- **libzip** - ZIP parsing for KMZ support
- **stb** - Image loading for textures
- **spdlog** - Logging framework

## 🛠️ Building

### Prerequisites

1. **Install Conan** (package manager)
   ```bash
   pip install conan
   ```

2. **Create Conan profile**
   ```bash
   conan profile detect --force
   ```

### Build Instructions

1. **Clone the repository**
   ```bash
   # git clone --recursive https://github.com/...
   # cd earth_map
   ```

2. **Install dependencies**
   ```bash
   conan install . --build=missing
   ```

3. **Configure CMake**
   ```bash
   cmake --preset conan-debug
   ```

4. **Build the project**
   ```bash
   cmake --build --preset conan-debug
   ```

5. **Run tests**
   ```bash
   cd build/Debug
   ctest --output-on-failure
   ```

### Build Options

- `-DEARTH_MAP_BUILD_TESTS=ON/OFF` - Build unit tests (default: ON)
- `-DEARTH_MAP_BUILD_EXAMPLES=ON/OFF` - Build example applications (default: ON)
- `-DEARTH_MAP_ENABLE_OPENGL_DEBUG=ON/OFF` - Enable OpenGL debug output (default: OFF)
- `-DEARTH_MAP_BUILD_DOCS=ON/OFF` - Generate documentation (default: OFF)

## 📖 Usage

### Basic Example

```cpp
#include <earth_map/earth_map.h>

int main() {
    // Create configuration
    earth_map::Configuration config;
    config.screen_width = 1920;
    config.screen_height = 1080;
    config.enable_performance_monitoring = true;
    
    // Create Earth Map instance
    auto earth_map = earth_map::EarthMap::Create(config);
    
    // Initialize
    if (!earth_map->Initialize()) {
        return -1;
    }
    
    // Load some data
    earth_map->LoadData("data/placemarks.kml");
    
    // Main render loop
    while (running) {
        earth_map->Render();
        // ... handle events, update camera, etc.
    }
    
    return 0;
}
```

### Loading Different Data Formats

```cpp
// Load KML file
earth_map->LoadData("path/to/placemarks.kml");

// Load KMZ file (compressed KML)
earth_map->LoadData("path/to/placemarks.kmz");

// Load GeoJSON file
earth_map->LoadData("path/to/geo_data.json");
```

### Camera Control

```cpp
auto* camera = earth_map->GetCameraController();

// Set camera position (longitude, latitude, altitude)
camera->SetPosition(-122.4194, 37.7749, 1000.0); // San Francisco

// Set camera target
camera->SetTarget(-122.4194, 37.7749, 0.0);

// Set camera heading, pitch, and roll
camera->SetOrientation(0.0, -45.0, 0.0);
```

## 🧪 Testing

Earth Map includes comprehensive test suites:

### Running Tests
```bash
# Run all tests
cd build/Release
ctest

# Run specific test
./earth_map_tests

# Run with verbose output
ctest --verbose

# Run performance benchmarks
./earth_map_benchmarks
```

### Test Categories
- **Unit Tests**: Component-level testing with mocks
- **Integration Tests**: System interaction validation
- **Performance Tests**: Automated performance regression testing
- **Memory Tests**: Valgrind memory leak detection

## 📊 Performance

### Benchmarks
- **1M+ points**: 30+ FPS on mid-range Android devices
- **Tile Loading**: <100ms tile switch time
- **Memory Usage**: <150MB for moderate datasets
- **Startup Time**: <2 seconds to initial render

### Optimization Features
- **Instanced Rendering**: Batch similar objects for GPU efficiency
- **Frustum Culling**: Skip off-screen objects
- **LOD Management**: Reduce detail for distant objects
- **Memory Pooling**: Reuse allocations to reduce overhead

## 🏗️ Architecture

### Core Components
- **Rendering Engine**: Tile and placemark rendering with shader management
- **Data Management**: Format parsers, spatial indexing, and caching
- **Performance Engine**: LOD management and GPU optimization
- **Platform Abstraction**: Cross-platform OpenGL and system utilities

### Directory Structure
```
earth_map/
├── include/earth_map/    # Public API headers
├── src/                  # Implementation source
├── tests/                # Unit and integration tests
├── examples/             # Sample applications
├── docs/                 # Documentation
├── tools/                # Development utilities
└── external/             # Third-party dependencies
```

## 📚 Documentation

- **[API Reference](docs/api/)** - Complete API documentation
- **[Tutorials](docs/tutorials/)** - Step-by-step guides
- **[Performance Guide](docs/performance/)** - Optimization tips
- **[Examples](examples/)** - Sample applications


### Development Setup

1. **Install development dependencies**
   ```bash
   pip install conan==2.0.16 clang-format cppcheck
   ```

2. **Run pre-commit checks**
   ```bash
   # Format code
   find include src tests -name "*.cpp" -o -name "*.h" -o -name "*.hpp" | xargs clang-format-14 -i
   
   # Static analysis
   cppcheck --enable=all include src tests
   ```

3. **Run full test suite**
   ```bash
   conan install . --build=missing -s build_type=Debug
   cmake --preset conan-default -DCMAKE_BUILD_TYPE=Debug -DEARTH_MAP_BUILD_TESTS=ON
   cmake --build --preset conan-debug
   cd build/Debug && ctest --output-on-failure
   ```

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- **Google Earth** - For setting the standard in 3D globe interaction
- **NASA World Wind** - For open-source inspiration and perfect perfomance
- **QGIS** - For GIS functionality benchmarks
- **Learn OpenGL** - For excellent OpenGL tutorials

## 📞 Support

- **GitHub Issues**: Report bugs and request features
- **Discussions**: Community discussions and questions
- **Documentation**: Complete API and usage documentation

---

**Earth Map** - Professional 3D GIS rendering library for modern applications.
