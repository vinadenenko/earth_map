cmake_minimum_required(VERSION 3.16)

project(qt-test-app VERSION 0.1 LANGUAGES CXX)

# Matches earth_map's own requirement (CMAKE_CXX_STANDARD 20 REQUIRED in the
# main CMakeLists.txt) so consuming its headers is well-defined.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.5)

# Resolves earth_map/0.1.0@utils/stable via conan and writes CMakeDeps
# config files under ${CMAKE_BINARY_DIR}/conan; must be on CMAKE_PREFIX_PATH
# before find_package(earth_map) can see them.
include(cmake/conan_handler.cmake)
handle_conan_deps()
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/conan")
find_package(earth_map REQUIRED)

qt_add_executable(appqt-test-app
    main.cpp
)

qt_add_qml_module(appqt-test-app
    URI qt-test-app
    VERSION 1.0
    QML_FILES
        Main.qml
    SOURCES
        EarthMapQuickItem.h
        EarthMapQuickItem.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appqt-test-app PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appqt-test-app
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appqt-test-app PRIVATE
    Qt6::Quick
    earth_map::earth_map
)

include(GNUInstallDirs)
install(TARGETS appqt-test-app
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
