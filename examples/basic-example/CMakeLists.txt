# Basic Example Application
cmake_minimum_required(VERSION 3.20)
project(basic_example)

# Find Earth Map library (if not built in same tree)
if(NOT TARGET earth_map)
    find_package(EarthMap REQUIRED)
endif()

# earth_map itself doesn't depend on glfw (it never creates its own window
# or GL context); this example does its own window/context creation, so it
# needs glfw directly rather than inheriting it from earth_map.
find_package(glfw3 REQUIRED)

# Create example executable
add_executable(basic_example
    basic_example.cpp
)

target_link_libraries(basic_example
    PRIVATE
        earth_map
        glfw
)

# libcurl has no default CA bundle location on Windows (unlike Linux, where
# it finds the system store automatically), so HTTPS tile requests fail peer
# verification there unless CURLOPT_CAINFO is set explicitly -- same root
# cause examples/qt-test-app works around for Android via InstallCaBundle().
# Baking the source-tree path in at configure time (rather than a runtime
# exe-relative lookup) keeps this working regardless of the current working
# directory the example is launched from. Linux/macOS libcurl already finds
# the system trust store, so this is Windows-only, same as qt-test-app's
# CA-bundle workaround is Android-only.
if(WIN32)
    target_compile_definitions(basic_example
        PRIVATE
            EARTH_MAP_EXAMPLE_CACERT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/resources/cacert.pem"
    )
endif()

# Add to examples group
set_target_properties(basic_example PROPERTIES FOLDER "Examples")