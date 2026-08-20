# Canonical Tile Rendering Design

## Geographic altitude contract

`Geographic::altitude` is always metres at the public API boundary. The globe
renderer uses normalized world radii, so the coordinate mapper is the sole
place where altitude crosses that boundary:

`world_radius = globe_radius * (1 + altitude_meters / EARTH_MEAN_RADIUS)`.

The inverse uses the corresponding inverse equation. Camera controls, presets,
and tile-zoom selection must consume this mapped world radius, never interpret
metres as normalized world units.

## Status

Proposed design. This document precedes changes to the renderer, tile
selection, and GPU indirection paths.

## Problem

The current renderer has three independently maintained representations of a
tile:

1. CPU visibility selection computes `TileCoordinates` from ray intersections.
2. The texture pool owns the uploaded GPU-array layer for a tile.
3. The per-zoom indirection texture maps a tile coordinate to that layer.

Historically, the third representation was not rebuilt after its window moved.
That synchronization path is now repaired and remains covered by the
residency invariant below.

In addition, CPU selection and the GLSL shader contain separate coordinate
conversion implementations. That cannot safely support WGS84 or selectable
tile projections over time.

## Design Principles

1. A geographic surface and a tile matrix set are explicit dependencies.
2. A `TileKey { tile_matrix_set_id, zoom, x, y }` is the only tile identity.
3. Tile residency is authoritative on the CPU. GPU indirection textures are
   derived acceleration structures, never independent state.
4. CPU and GPU must evaluate the same mapping:

   `screen ray -> surface point -> geographic coordinate -> TileKey`.

   The ray origin is derived from the inverse view transform. A separately
   stored camera position is not an additional source of truth and must not
   participate in tile selection or shader sampling.

   CPU selection must build a conservative envelope from the viewport's
   surface-ray footprint. Four frustum corners are insufficient for an
   oblique perspective view because geographic extrema can occur between
   corners.

5. The render mesh approximates the surface for rasterization; it does not
   define geodetic location or tile identity.
6. Viewport dimensions are part of the render contract. A resize must update
   both the OpenGL viewport and the renderer's projection input before the
   next frame. Updating only `glViewport` creates different CPU and GPU rays.

## Reproducible High-Zoom Cases

The example application provides deterministic investigation presets:

| Key | Location | Target zoom | Altitude |
| --- | --- | ---: | ---: |
| `2` | Yerevan, Armenia | 18 | 800 m |
| `Shift+2` | Yerevan, Armenia | 19 | 400 m |
| `3` | Johannesburg, South Africa | 18 | 800 m |
| `Shift+3` | Johannesburg, South Africa | 19 | 400 m |

The altitudes follow the current logarithmic zoom rule exactly: 800 m selects
z18 and 400 m selects z19. These presets are for reproducible diagnostics;
they are not part of the public renderer API.

## Coordinate Model

### Geographic Surface

Introduce a `GeodeticSurface` interface in the render coordinate frame:

```cpp
class GeodeticSurface {
 public:
  virtual ~GeodeticSurface() = default;
  virtual std::optional<SurfacePoint> IntersectRay(
      const WorldRay& ray) const = 0;
  virtual Geographic ToGeographic(const SurfacePoint& point) const = 0;
  virtual WorldPoint ToWorld(const Geographic& coordinate) const = 0;
};
```

The default production implementation is `Wgs84EllipsoidSurface`. It uses the
WGS84 semi-major axis and flattening for ray intersection and geodetic
conversion. Rendering units may remain normalized, but the normalization
transform is part of the surface/frame implementation rather than implicit
renderer math.

A sphere implementation remains useful for tests and low-cost configurations,
but it is selected explicitly.

### Tile Matrix Set

A projection alone does not fully describe tile numbering. Add a
`TileMatrixSet` abstraction:

```cpp
class TileMatrixSet {
 public:
  virtual ~TileMatrixSet() = default;
  virtual TileKey GeographicToTile(const Geographic&, int zoom) const = 0;
  virtual TileBounds TileBoundsFor(const TileKey&) const = 0;
  virtual int MinZoom() const = 0;
  virtual int MaxZoom() const = 0;
  virtual std::string_view Id() const = 0;
};
```

The first supported implementation is `WebMercatorQuad` (XYZ, EPSG:3857),
which is compatible with the existing OSM provider. Future examples include
`WorldCRS84Quad` and provider-defined matrix sets.

The existing `Projection` types remain useful beneath a tile matrix set, but
are not by themselves responsible for X/Y indexing, Y direction, origin, or
matrix dimensions.

## GPU Contract

The GPU receives a projection-program variant corresponding to the selected
supported tile matrix set. It calculates a surface intersection from the
fragment ray and converts that point to the same `TileKey` as the CPU.

Arbitrary C++ projection implementations cannot automatically execute in GLSL.
Selectable projections must therefore be one of:

1. built-in CPU/GLSL-paired tile matrix sets; or
2. a projection plugin that supplies both CPU implementation and validated GLSL
   implementation.

This is intentional: it prevents a CPU-only projection choice from silently
selecting tiles that the GPU samples differently.

## Residency and Indirection Invariant

`TileResidency` owns the authoritative mapping:

```text
TileKey -> texture-array layer
```

For an active window `W` at zoom `z`, the indirection texture must satisfy:

```text
for each resident key k with k.zoom == z and k in W:
  indirection[k - W.offset] == residency[k].layer
```

When the window changes, its GPU texture is cleared or shifted, then rebuilt
from resident tiles within the new window before the draw that uses it. A tile
outside the window remains resident; it is mapped again if a later window
contains it.

`Loaded` means both that a pool layer exists and that the residency table owns
the layer. It must not be interpreted as proof that a tile is currently
addressable through a particular indirection window.

## Implementation Sequence

1. Repair indirection synchronization: after every window update, repopulate
   entries from the resident pool layers. Add an explicit distinction between
   `resident` and `mapped_in_active_window` for diagnostics.
2. Build the CPU footprint from viewport rays rather than four frustum
   corners, using the surface mapping shared with the GPU. The current grid is
   a conservative interim envelope; replace it with an adaptive footprint or
   tile-quadtree/frustum culler before treating selection as mathematically
   exact. Do not rely on a renderer-only percentage expansion as the
   correctness mechanism.
3. Introduce `TileKey`/`TileMatrixSet`, adapting existing Web Mercator code as
   `WebMercatorQuad` without changing provider URLs or cache keys unexpectedly.
4. Introduce `Wgs84EllipsoidSurface` and route CPU screen-ray selection through
   it.
5. Replace hard-coded shader tile math with paired `WebMercatorQuad` GLSL and
   an analytic surface-ray intersection. Add further paired variants only with
   validation.
6. Upgrade visible mesh tessellation/terrain handling independently. Geometry
   quality must not change tile identity.

## Required Tests

1. CPU/GLSL reference tests for `WebMercatorQuad` at tile centers, boundaries,
   the date line, and Web Mercator latitude limits.
2. WGS84 ellipsoid ray-intersection and geodetic round-trip tests.
3. Screen-grid tests: CPU and GPU reference paths return the same TileKey for
   sampled pixels.
4. Indirection property tests across arbitrary window shifts and complete
   clears: all resident tiles inside the active window are mapped to their
   actual texture-pool layer.
5. Regression test for the observed state: every selected tile can be
   resident, while an indirection clear previously made it unreachable.
