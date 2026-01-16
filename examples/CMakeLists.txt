# Basic Example Application
cmake_minimum_required(VERSION 3.20)
project(basic_example)

# Find Earth Map library (if not built in same tree)
if(NOT TARGET earth_map)
    find_package(EarthMap REQUIRED)
endif()

# Create example executable
add_executable(basic_example
    basic_example.cpp
)

target_link_libraries(basic_example
    PRIVATE
        earth_map
)

# Add to examples group
set_target_properties(basic_example PROPERTIES FOLDER "Examples")