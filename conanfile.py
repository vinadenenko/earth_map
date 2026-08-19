from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
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
        "with_tests": False,
        "with_examples": False,
        "enable_opengl_debug": False
    }

    # Export sources for conan center
    # exports_sources = "CMakeLists.txt", "src/*", "include/*", "tests/*", "examples/*"
    exports_sources = "*", "!build/*"

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

        # GLEW loads the library's own desktop-GL entry points at
        # compile/link time (see #include <GL/glew.h> throughout
        # src/renderer/*.cpp). Not usable on Android: GLEW's own header
        # pulls in glu.h, and no Android-compatible GLU exists (its conan
        # recipe only offers "mesa-glu", which needs a desktop GL found via
        # pkg-config, or "system", which Android has neither). Android
        # instead links GLESv3 directly -- see CMakeLists.txt's Android
        # branch and the __ANDROID__ conditional includes in src/renderer/.
        if self.settings.os != "Android":
            self.requires("glew/2.2.0")

        # GLFW is only used for window/context creation in
        # examples/basic-example; the library itself never calls into it.
        # Keeping it out of earth_map's own dependency graph when examples
        # aren't being built means platforms without a usable glfw backend
        # (e.g. Android) can still build and consume earth_map itself.
        if self.options.with_examples:
            self.requires("glfw/3.3.8")

        # Mathematics library. transitive_headers=True: glm/glm.hpp (and
        # friends) appear directly in ~20 public earth_map headers, so
        # consumers linking earth_map::earth_map need glm's include dirs
        # too, not just earth_map's own build.
        self.requires("glm/1.0.1", transitive_headers=True)

        # JSON parsing for configuration and data formats. Not used in any
        # public header (only in .cpp files), so no transitive_headers --
        # consumers don't need nlohmann_json's include dirs.
        self.requires("nlohmann_json/3.11.2")

        # XML parsing for KML support
        self.requires("pugixml/1.14")

        # ZIP parsing for KMZ support
        self.requires("libzip/1.10.1")

        # Image loading for textures and icons. transitive_headers=True:
        # stb_image.h appears in the public
        # renderer/texture_atlas/tile_load_worker_pool.h header.
        self.requires("stb/cci.20230920", transitive_headers=True)

        # Logging framework
        self.requires("spdlog/1.13.0")

        # Network requests
        self.requires("libcurl/7.87.0")

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
        cmake = CMakeDeps(self)
        cmake.generate()

        tc = CMakeToolchain(self)
        tc.variables["EARTH_MAP_BUILD_TESTS"] = self.options.with_tests
        tc.variables["EARTH_MAP_BUILD_EXAMPLES"] = self.options.with_examples
        tc.variables["EARTH_MAP_ENABLE_OPENGL_DEBUG"] = self.options.enable_opengl_debug
        tc.generate()

    def build(self):
        """Build the project"""
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
        elif self.settings.os == "Android":
            # Matches the Android branch of target_link_libraries(earth_map
            # ...) in CMakeLists.txt. earth_map is a static library, so
            # linking it never actually resolves symbols like
            # glDeleteVertexArrays/glTexImage3D against GLESv3 during its
            # own build -- only a consumer's final executable/shared-lib
            # link does, so consumers need this propagated here too.
            self.cpp_info.system_libs.extend(["GLESv3", "EGL", "android", "log"])

        # Define targets for proper transitive dependencies
        self.cpp_info.set_property("cmake_target_name", "earth_map::earth_map")

        # Include directories
        self.cpp_info.includedirs = ["include"]

        # Build type specific flags
        if self.settings.build_type == "Debug":
            self.cpp_info.defines.append("EARTH_MAP_DEBUG")

        if self.options.enable_opengl_debug:
            self.cpp_info.defines.append("EARTH_MAP_OPENGL_DEBUG")
