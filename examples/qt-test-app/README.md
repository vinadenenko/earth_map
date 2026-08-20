# Qt Test App

A standalone Qt Quick/QML application demonstrating how to embed `earth_map`
in a QML scene via a custom `QQuickItem` (`EarthMapQuickItem`), following
Qt's own "Scene Graph - OpenGL Under QML" pattern
(`QQuickWindow::beforeRendering()`/`beforeRenderPassRecording()`) rather
than `QQuickFramebufferObject`.

## This is not built by earth_map's own CMake project

It lives under `examples/` for discoverability, but the root
`CMakeLists.txt`'s `add_subdirectory(examples/basic-example)` only reaches
`examples/basic-example/`, not this directory, and it isn't gated behind
`EARTH_MAP_BUILD_EXAMPLES` the way `examples/basic-example/basic_example.cpp`
is. It has its own `project()` call and its own conan integration
(`cmake/conan_handler.cmake`) and is meant to be opened and built as a
**completely separate project** -- e.g. via Qt Creator's "Open Project...",
pointed directly at this directory's `CMakeLists.txt`.

Do **not** try to build it with earth_map's own workflow
(`conan install . --build=missing` / `cmake --preset conan-debug` /
`cmake --build --preset conan-debug` from the repo root). That workflow
configures earth_map itself; it has no effect here.

## Prerequisite: earth_map must already be conan-installed

`cmake/conan_handler.cmake` *consumes* earth_map as a conan package under
the exact reference `earth_map/0.1.0@utils/stable` -- it does not build
earth_map from source. Before opening/configuring this project, install
earth_map into your local conan cache under that reference:

```bash
conan create <path-to-earth_map-repo-root> --user=utils --channel=stable --build=missing
```

If that reference isn't in your local conan cache, configuring this
project will fail immediately at the conan dependency-resolution step.

## Layout

- `EarthMapQuickItem.h` / `.cpp` -- the actual embedding code: owns the
  `earth_map::EarthMap` instance, forwards Qt mouse/keyboard/wheel events
  to it, and scopes GL rendering to the item's own QML geometry.
- `main.cpp` / `Main.qml` -- ordinary Qt Quick application entry point.
