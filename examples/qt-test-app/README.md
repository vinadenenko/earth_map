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

## Deterministic performance scenarios

The in-app panel provides two repeatable benchmarks. Both lock camera input,
warm up for two seconds, suppress the live QML performance chart while they
run, retain samples in memory, and write one JSON report when they finish.
The report contains every sampled frame plus p50/p95/max summaries for frame
CPU/GPU time and every renderer timing zone.

- `Run steady z13` holds a fixed Yerevan z13 camera for ten seconds. Use it
  to isolate selection and draw cost from movement and tile streaming.
- `Run flight` follows the same fixed Armenia zoom-and-pan route as the basic
  example's scripted performance flight. Use it to compare streaming and
  cache behaviour.

The app shows the report's absolute path after completion. Reports are stored
under `earth-map-performance` in Qt's `AppDataLocation`. On a debuggable
Android build, copy a report without granting storage permissions:

```bash
adb shell run-as <package-id> ls files/earth-map-performance
adb exec-out run-as <package-id> cat files/earth-map-performance/<report>.json > report.json
```

Keep device model, screen resolution, build type, thermal state, network
state, and scenario choice fixed when comparing reports. `frame_gpu_ms` and
per-zone GPU values are absent when the device lacks
`GL_EXT_disjoint_timer_query`; CPU values remain valid.
