# WGS84/ECEF camera and render rewrite

## Status

Active architectural rewrite. The normalized unit-sphere renderer is legacy
code and must be removed, not retained through compatibility adapters.

The target is a physically coherent WGS84 globe in which imagery, terrain,
placemarks, camera controls, selection, picking, and GPU rendering use one
coordinate contract.

## Non-negotiable coordinate contract

```text
Public application API
  WGS84 geodetic: latitude/longitude in documented public units, height in metres,
  plus explicit altitude mode where terrain is involved.

CPU geometry, camera, selection, terrain, and picking
  WGS84 geodetic/ECEF double precision metres.

GPU render input
  float local ENU coordinates relative to a per-frame camera ECEF origin.
```

Web Mercator is only an imagery tile-matrix projection. It must never be used
for physical geometry, camera state, terrain, placemarks, or spatial indexing.

The existing `coordinates::World`, `NORMALIZED_GLOBE_RADIUS`, and metre-to-unit
sphere conversion paths are legacy. Do not add new call sites. Delete them as
the corresponding subsystem is cut over.

## End-user API rules

- Applications communicate geographic positions through WGS84 coordinates.
- Public height is metres, never normalized render units.
- Point, line, polygon, terrain, camera, and pick APIs must state altitude
  semantics explicitly: absolute ellipsoid, clamp-to-terrain, or
  terrain-relative.
- ECEF, ENU, floating origins, GPU precision, and tile addressing stay
  implementation details.

## Target camera contract

Camera state is stored as double-precision WGS84/ECEF metres. It exposes:

- geographic position/target for applications;
- ECEF position/target for internal selection and terrain;
- a private `renderer::EcefRenderFrame` whose origin is the current camera;
- a local ENU `glm::mat4` view matrix used only with positions already converted
  into that frame;
- metre-based near/far clipping planes and metres-per-second controls.

The camera must not expose a normalized `glm::vec3` world position after the
cutover. Rendering call sites that need a GPU value use a local-frame float
conversion, not a global float ECEF coordinate.

## Required implementation order

### 1. Camera/controller contract cutover

Replace normalized `glm::vec3` camera position and target interfaces in:

- `include/earth_map/renderer/camera.h`
- `include/earth_map/core/camera_controller.h`
- `src/renderer/camera.cpp`
- `src/core/camera_controller.cpp`

Camera navigation, animation, pan, zoom, orbit, clipping, and ray construction
must work in metre-based ECEF/ENU space. Do not keep old normalized overloads.

### 2. Renderer frame propagation

`RendererImpl` obtains the camera ECEF render frame once per frame and passes it
to every render subsystem. The view matrix is ENU-local; the projection matrix
uses metre clipping distances. Performance/picking code receives the same frame.

### 3. Globe, imagery, and elevation geometry

- Globe fallback geometry is WGS84 ellipsoid ECEF, not an icosphere of radius 1.
- Geographic imagery patches are generated as ECEF doubles and converted to
  local float ENU vertices for the active frame.
- Elevation stays metres and offsets the WGS84 ellipsoid normal in ECEF.
- Remove normalized elevation displacement and `NormalizedRenderPosition`.

### 4. Physical selection and interaction

- Tile and placemark selection use ECEF frusta and WGS84 horizon tests.
- Screen rays are local ENU rays transformed to ECEF, then intersect the WGS84
  ellipsoid or terrain.
- `MapInteraction` stops using normalized-sphere `CoordinateMapper` methods.
- Mini-map derives geographic camera location directly from ECEF/WGS84.

### 5. Placemark renderer

Only after steps 1–4, render selected point placemarks through an ECEF render
frame. The point renderer consumes immutable placemark snapshots and private
selector output. Screen clustering, labels, picking, lines, polygons, terrain
anchoring, and custom handlers are separate later phases.

## Existing work that is already compatible

- `geodesy::Wgs84Ellipsoid` is authoritative for geodetic/ECEF/ENU conversion.
- `renderer::EcefRenderFrame` is the private local-frame primitive.
- Placemark public data uses `geodesy::GeodeticPosition` and never accepts
  normalized render coordinates.
- Private placemark spatial index and selector operate in ECEF metres.

## Completion criteria

The rewrite is complete only when repository search finds no active rendering,
camera, interaction, terrain, or placemark use of normalized globe-space APIs.
`CoordinateMapper` may retain Web Mercator imagery helpers, but its normalized
sphere methods must not be on an active render path.
