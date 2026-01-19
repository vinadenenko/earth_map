from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy
import os


class EarthMapConan(ConanFile):
    name = "earth_map"
    version = "0.1.0"
    description = "High-performance 3D tile map renderer for GIS applications"
    author = "Earth Map Team"
    url = "https://github.com/earth-map/earth_map"
    license = "MIT"
    package_type = "library"

    # Package configuration
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_tests": [True, False],
        "with_examples": [True, False],
        "enable_opengl_debug": [True, False]
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_tests": True,
        "with_examples": True,
        "enable_opengl_debug": False
    }

    # Export sources for conan center
    exports_sources = "CMakeLists.txt", "src/*", "include/*", "tests/*", "examples/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        """Core dependencies for Earth Map library"""

        # Core OpenGL and windowing
        self.requires("glfw/3.3.8")
        self.requires("glew/2.2.0")

        # Mathematics library
        self.requires("glm/1.0.1")

        # JSON parsing for configuration and data formats
        self.requires("nlohmann_json/3.11.2")

        # XML parsing for KML support
        self.requires("pugixml/1.14")

        # ZIP parsing for KMZ support
        self.requires("libzip/1.10.1")

        # Image loading for textures and icons
        self.requires("stb/cci.20230920")

        # Logging framework
        self.requires("spdlog/1.13.0")

        # Network requests
        self.requires("libcurl/8.17.0")

        # Testing framework (when tests are enabled)
        if self.options.with_tests:
            self.requires("gtest/1.14.0")
            self.requires("benchmark/1.8.3")

        # Profiling and debugging (when enabled)
        if self.options.enable_opengl_debug:
            self.requires("tracy/0.10.0")

    def build_requirements(self):
        """Build-time requirements"""
        self.tool_requires("cmake/[>=3.20]")

    def generate(self):
        """Generate CMake toolchain and dependencies"""
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.variables["EARTH_MAP_BUILD_TESTS"] = self.options.with_tests
        tc.variables["EARTH_MAP_BUILD_EXAMPLES"] = self.options.with_examples
        tc.variables["EARTH_MAP_ENABLE_OPENGL_DEBUG"] = self.options.enable_opengl_debug
        tc.generate()

    def build(self):
        """Build the project"""
        from conan.tools.cmake import CMake, cmake_program
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

        if self.options.with_tests:
            cmake.test()

    def package(self):
        """Package the library"""
        copy(self, "*.h", src=os.path.join(self.source_folder, "include"), dst=os.path.join(self.package_folder, "include"))

        if self.options.shared:
            copy(self, "*.dll", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"), keep_path=False)
            copy(self, "*.dylib*", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
            copy(self, "*.so*", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        else:
            copy(self, "*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
            copy(self, "*.lib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        """Provide package information to consumers"""
        self.cpp_info.libs = ["earth_map"]

        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs.extend(["GL", "X11", "Xrandr", "Xinerama", "Xi", "Xcursor", "dl", "pthread"])
        elif self.settings.os == "Windows":
            self.cpp_info.system_libs.extend(["opengl32", "gdi32", "user32", "kernel32", "shell32"])
        elif self.settings.os == "Macos":
            self.cpp_info.frameworks.extend(["OpenGL", "Cocoa", "IOKit", "CoreVideo"])

        # Define targets for proper transitive dependencies
        self.cpp_info.set_property("cmake_target_name", "earth_map::earth_map")

        # Include directories
        self.cpp_info.includedirs = ["include"]

        # Build type specific flags
        if self.settings.build_type == "Debug":
            self.cpp_info.defines.append("EARTH_MAP_DEBUG")

        if self.options.enable_opengl_debug:
            self.cpp_info.defines.append("EARTH_MAP_OPENGL_DEBUG")
