cmake_minimum_required(VERSION 3.20)
project(earth_map
    VERSION 0.1.0
    DESCRIPTION "High-performance 3D tile map renderer for GIS applications"
    LANGUAGES CXX
)

# C++ standard and project options
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(EARTH_MAP_BUILD_TESTS "Build unit tests" ON)
option(EARTH_MAP_BUILD_EXAMPLES "Build example applications" ON)
option(EARTH_MAP_ENABLE_OPENGL_DEBUG "Enable OpenGL debug output" OFF)
option(EARTH_MAP_BUILD_DOCS "Generate documentation" OFF)
option(EARTH_MAP_INSTALL "Generate install target" ON)


# list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
# list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

# if (CONAN_EXPORTED)
# else ()
#     if (NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
#         message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
#         file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/0.18.1/conan.cmake"
#                 "${CMAKE_BINARY_DIR}/conan.cmake"
#                 TLS_VERIFY ON)
#     endif ()

#     include(${CMAKE_BINARY_DIR}/conan.cmake)

#     conan_cmake_autodetect(settings)
#     conan_cmake_install(PATH_OR_REFERENCE ${PROJECT_SOURCE_DIR}/conanfile.py
#             OUTPUT_FOLDER ${CMAKE_BINARY_DIR} BUILD missing SETTINGS ${settings})
# endif ()
# include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake)

# include_directories(${CMAKE_CURRENT_LIST_DIR}/include ${CMAKE_CURRENT_LIST_DIR}/src)

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "11.0")
        add_compile_options(-Wno-stringop-overflow)
    endif()
endif()

# Debug/Release configurations
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(EARTH_MAP_DEBUG)
    if(EARTH_MAP_ENABLE_OPENGL_DEBUG)
        add_compile_definitions(EARTH_MAP_OPENGL_DEBUG)
    endif()
endif()

# Find required packages
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(pugixml REQUIRED)
find_package(libzip REQUIRED)
find_package(stb REQUIRED)
find_package(spdlog REQUIRED)
find_package(CURL REQUIRED)

# Optional packages for testing and examples
if(EARTH_MAP_BUILD_TESTS)
    find_package(GTest REQUIRED)
    find_package(benchmark REQUIRED)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
file(GLOB_RECURSE EARTH_MAP_SOURCES 
    "src/*.cpp"
    "src/*.c"
)

file(GLOB_RECURSE EARTH_MAP_HEADERS
    "include/*.h"
    "include/*.hpp"
)

# Shader sources (to be embedded in the binary)
file(GLOB_RECURSE SHADER_SOURCES "src/shaders/*.vert" "src/shaders/*.frag" "src/shaders/*.geom" "src/shaders/*.comp")

# Create the main library
add_library(earth_map ${EARTH_MAP_SOURCES} ${EARTH_MAP_HEADERS} ${SHADER_SOURCES})

# Set library properties
set_target_properties(earth_map PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${EARTH_MAP_HEADERS}"
    CXX_VISIBILITY_PRESET hidden
)

# Link libraries
target_link_libraries(earth_map
    PUBLIC
        OpenGL::GL
        glfw
        GLEW::GLEW
        glm::glm
        nlohmann_json::nlohmann_json
        pugixml::pugixml
        libzip::zip
        stb::stb
        spdlog::spdlog
        CURL::libcurl
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(earth_map PUBLIC opengl32 gdi32 user32 kernel32 shell32)
elseif(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    target_link_libraries(earth_map PUBLIC ${COCOA_FRAMEWORK} ${IOKIT_FRAMEWORK} ${COREVIDEO_FRAMEWORK})
else()
    target_link_libraries(earth_map PUBLIC X11 Xrandr Xinerama Xi Xcursor dl pthread)
endif()

# Include directories for the library
target_include_directories(earth_map
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compile definitions
target_compile_definitions(earth_map
    PRIVATE
        EARTH_MAP_BUILDING
    PUBLIC
        EARTH_MAP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        EARTH_MAP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        EARTH_MAP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        EARTH_MAP_VERSION="${PROJECT_VERSION}"
)

# Generate shader header if needed
if(SHADER_SOURCES)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders.h
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.py
                ${SHADER_SOURCES}
                ${CMAKE_CURRENT_BINARY_DIR}/shaders.h
        DEPENDS ${SHADER_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shaders.py
        COMMENT "Embedding shader sources into header"
    )
    target_sources(earth_map PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shaders.h)
    target_include_directories(earth_map PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()

# Tests
if(EARTH_MAP_BUILD_TESTS)
    enable_testing()
    
    # Test sources
    file(GLOB_RECURSE TEST_SOURCES "tests/unit/*.cpp" "tests/integration/*.cpp" "tests/data/*.cpp" "tests/renderer/*.cpp")

    # Create test executable
    add_executable(earth_map_tests ${TEST_SOURCES})
    
    target_link_libraries(earth_map_tests
        PRIVATE
            earth_map
            GTest::gtest
            GTest::gtest_main
            benchmark::benchmark
    )
    
    target_include_directories(earth_map_tests PRIVATE tests)
    
    # Add test to CTest
    add_test(NAME UnitTests COMMAND earth_map_tests)
    
    # Benchmark test
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/performance")
        file(GLOB_RECURSE PERF_SOURCES "tests/performance/*.cpp")
        if(PERF_SOURCES)
            add_executable(earth_map_benchmarks ${PERF_SOURCES})
            target_link_libraries(earth_map_benchmarks
                PRIVATE
                    earth_map
                    benchmark::benchmark
            )
            target_include_directories(earth_map_benchmarks PRIVATE tests)
        endif()
    endif()
endif()

# Examples
if(EARTH_MAP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Documentation
if(EARTH_MAP_BUILD_DOCS)
    find_package(Doxygen QUIET)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
        set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs)
        
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in
            ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            @ONLY
        )
        
        add_custom_target(docs
            ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    endif()
endif()

# Installation
if(EARTH_MAP_INSTALL)
    include(GNUInstallDirs)
    
    install(TARGETS earth_map
        EXPORT EarthMapTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/earth_map
    )
    
    install(DIRECTORY include/earth_map
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )
    
    # Export the targets
    install(EXPORT EarthMapTargets
        FILE EarthMapTargets.cmake
        NAMESPACE earth_map::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EarthMap
    )
    
    # Generate config files
    include(CMakePackageConfigHelpers)
    
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EarthMapConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/EarthMapConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EarthMap
    )
    
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/EarthMapConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/EarthMapConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/EarthMapConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EarthMap
    )
endif()

# CPack configuration for packaging
include(CPack)
set(CPACK_PACKAGE_NAME "EarthMap")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VENDOR "Earth Map Team")
set(CPACK_PACKAGE_CONTACT "earth-map@example.com")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
endif()
