# Industry-Standard Globe Renderer: Recovery and Implementation Plan

## Status and authority

**Status:** approved architectural direction; implementation starts only from a
verified baseline.

This document replaces the earlier refactor plan.  That plan incorrectly
removed the working globe, imagery streaming, and terrain paths before a
replacement existed.  It is not to be followed.

The target is a professional globe renderer, informed by Cesium/3D Tiles style
systems, but built incrementally in this codebase and on its existing OpenGL
renderer.  It does **not** require Vulkan or a wholesale framework rewrite.

Primary product sequence:

1. Restore and prove reliable streamed globe imagery.
2. Move the production surface to geographic quadtree patches.
3. Integrate the existing terrain capability into those same patches.
4. Add the performance, scheduling, and validation machinery needed for high
   zoom imagery and terrain.

## Non-negotiable engineering rules

1. A phase is not complete until the repository builds and its agreed tests
   pass.  A non-buildable repository may exist only during an active edit, not
   at a phase boundary and never as a proposed commit.
2. After every logical phase, stop.  Report the evidence, visual result, known
   limitations, and one proposed commit message.  Do not make the commit unless
   the user asks.
3. Do not add backward-compatibility wrappers, duplicate public APIs, or
   permanent feature flags.  A replacement becomes the only production path
   when it passes its acceptance criteria; then obsolete code is removed.
4. Do preserve a working renderer while its replacement is being built.  This
   is a migration safety boundary, not a second permanent implementation.
5. No renderer decision may rely on duplicated, independently maintained CPU
   and GLSL geographic math.  There must be one specified coordinate contract
   and conformance tests for every CPU/GPU boundary.
6. User worktree changes are never reverted, reformatted, or folded into this
   work without explicit approval.
7. Debug probes are temporary and off by default.  Each has a removal phase or
   becomes a bounded test/telemetry metric; continuous per-frame log spam is
   not acceptable.

## Target architecture

### 1. World, surface, and precision model

The authoritative physical model is the WGS84 reference ellipsoid.

| Concern | Authoritative representation |
| --- | --- |
| World positions | WGS84 ECEF Cartesian coordinates, metres, CPU `double` |
| Geographic positions | longitude/latitude in radians; height in metres with an explicit datum |
| Camera/navigation | ECEF plus a local ENU / camera-relative render frame |
| GPU positions | camera-relative `float` coordinates derived from one frame snapshot |
| Terrain height | metres, with source datum documented and converted explicitly if necessary |
| Tile identity | integers: source matrix set + level + x + y; never float world coordinates |

`GeoProject` / `GeoUnproject` (and the WGS84 ellipsoid implementation beneath
them) are the source of truth.  Haversine is not permitted in rendering,
selection, culling, or tile addressing.  It may exist only as a clearly named,
tested approximate UI utility if a future non-rendering feature needs it.

The renderer must not use normalized sphere coordinates as its authoritative
surface or tile coordinate system.  They may temporarily remain inside the
old icosphere fallback implementation until that fallback is removed.

### 2. Production surface: geographic quadtree patches

The final renderable globe surface is a hierarchy of WGS84 geographic patches,
not a globally rebuilt icosphere.

Each patch has:

```text
TerrainPatchKey { level, x, y }       // geographic quadtree identity
GeographicRect   { west, south, east, north }  // radians
PatchBounds      // WGS84/ECEF conservative bound
geometricErrorMeters
parent / four children
```

Patch mesh vertices are generated from the WGS84 ellipsoid in the patch's
geographic rectangle.  A terrain source later displaces exactly these vertices
by height.  Thus imagery works first on the ellipsoid, and terrain does not
require another globe representation or a second imagery path.

Selection is screen-space-error (SSE) based:

```text
pixelError = geometricErrorMeters / distanceToPatchMeters * projectionScale
```

Refine when the pixel error exceeds the configured budget, retain parents as
fallback until children are ready, enforce neighbour-compatible levels (or use
stitching/skirting), and use temporal coherence to avoid refinement thrash.

The existing icosphere is retained only as the **recovery/coarse fallback**
while this surface is introduced.  It is not the final high-zoom imagery or
terrain topology.  This preserves a verified globe during migration without
making two surface models permanent.

### 3. Imagery: matrix-set aware virtual texturing

Imagery sources are independent from terrain patches.  An imagery source
declares its own tile matrix set, e.g. Web Mercator XYZ, including tile size,
level range, x wrapping, y convention, and valid latitude range.

```text
ImageTileKey {
  imagerySourceId,
  matrixSetId,
  level, x, y
}
```

The virtual-texture system remains.  It contains:

```text
provider -> byte/disk cache -> decode cache -> request scheduler
         -> render-thread upload queue -> physical texture-array cache
         -> GPU page table / indirection window -> shader sampling
```

The page table maps a virtual imagery address to a physical texture-array
layer.  It is a derived GPU representation of the residency manager's
authoritative integer mapping, not a separate cache.

Global full-resolution page tables are not allocated.  The GPU uses a
camera/patch-local indirection window (or equivalent hierarchical page-table
representation) with integer tile origins.  Shaders derive local coordinates
from the selected geographic patch, avoiding large global float calculations
at z18-z21 and beyond.

Parent imagery is sampled as the normal fallback whenever a child page is not
resident.  Missing imagery must never turn a valid globe surface gray; it must
show an ancestor, a deterministic placeholder, or a coarse base layer.

### 4. Streaming, cache, and eviction

The existing provider/loader/cache/upload responsibilities are retained and
made explicit rather than deleted:

- `ImageProvider`: extensible source interface for XYZ/HTTP, offline/MBTiles,
  WMTS, and future sources.
- byte and disk cache: avoid repeated network/file work.
- decoded-image cache: bounded CPU-memory cache.
- request scheduler: priority, cancellation, deduplication, and prefetch.
- render-thread GPU upload queue: the only path that creates, modifies, or
  deletes GL resources.
- residency manager: owns physical layers and GPU page-table updates.

Eviction is visibility-aware and LRU/priority based.  Visible pages, required
ancestor fallbacks, pages in an in-flight draw snapshot, and pages currently
being uploaded are pinned.  Candidates outside the view and outside prefetch
priority are evicted first.  This preserves the useful behaviour of the old
tile cache while making its invariants testable.

### 5. Frame ownership and rendering

At the start of a frame, the update side creates an immutable `RenderSnapshot`:

```text
camera ECEF / local render frame
projection parameters
selected terrain patches and their transforms
imagery layer/page-table bindings
draw-residency pins
```

The render thread consumes one snapshot and does not inspect live scheduler,
camera, or cache state while drawing.  Uploads and page-table updates occur at
a defined render-thread point before the snapshot that uses them is submitted.

This eliminates state races such as a CPU selection agreeing with cache state
while a shader sees a different page-table generation.

### 6. Terrain integration

Terrain is a `TerrainPatchSource` / prepared patch data pipeline, not a special
case inside imagery.

```text
TerrainPatchSource -> decode/sample -> prepared heights + min/max/error
                   -> render-thread terrain patch resources
```

The current SRTM/elevation manager becomes the first adapter.  Its output is
converted to documented metres and datum before it affects WGS84 patch
vertices.  Future quantized-mesh, 3D Tiles terrain, or higher-resolution DEM
sources fit behind the same boundary.

Terrain and imagery choose levels independently.  Terrain uses SSE/geometric
error; imagery requests enough source resolution for the patch's projected
screen area.  A terrain refinement must not force a global imagery reload, and
an imagery refinement must not rebuild globe geometry.

### 7. Validation and observability

The renderer needs evidence, not visual guesses.

- unit tests for WGS84 conversion, geographic rectangles, tile matrix sets,
  wrapping, parent/child relations, and page-table addressing;
- CPU/GLSL conformance fixtures for localized patch-to-imagery coordinates at
  z13, z18, and z21;
- deterministic camera presets with screenshot/reference checks where the
  test environment supports them;
- integration tests for parent fallback, upload ordering, cancellation, and
  eviction pinning;
- bounded counters for queued/loading/decoded/resident/visible pages,
  evictions, ancestor fallbacks, terrain patch SSE, and frame timing.

Diagnostic output is opt-in and event-oriented.  Per-frame messages such as
`picked zoom`, candidate count, and selected tile count are disabled unless a
specific investigation enables them.

## Implementation phases

Every phase below ends with: build, relevant automated tests, visual smoke
test, brief report, and a proposed commit message.  Do not proceed until the
user authorizes the next phase.

### Phase 0 — Baseline capture and recovery contract

**Goal:** establish exactly what the checked-out baseline builds and renders.

- Create a clean build directory and record the toolchain/configuration.
- Run the existing tests.
- Run the basic example with the existing imagery provider and current SRTM
  data when available.
- Capture known camera presets: whole globe, ordinary city zoom, and high
  zoom.  Record existing defects separately from regressions.
- Inventory the existing provider, loader, caches, upload queue, page table,
  icosphere, elevation path, and threading boundaries.

**Acceptance:** build succeeds; the baseline's actual output is documented;
no production behaviour changes.

**Proposed commit:** `docs: capture globe renderer baseline and invariants`

### Phase 1 — Establish the WGS84 coordinate contract

**Goal:** make the physical and geographic contract executable and testable.

- Introduce/centralize WGS84 ECEF, geodetic, ENU, and datum types with clear
  units.
- Route authoritative project/unproject operations through those types.
- Add tests for round trips, poles, anti-meridian, ellipsoid surface, and
  known geographic fixtures.
- Identify every normalized-sphere conversion in the old renderer as either
  fallback-only or migration work; do not silently reuse it as authority.

**Acceptance:** no rendering regression; tests prove the coordinate contract.

**Proposed commit:** `feat: establish WGS84 coordinate contract`

### Phase 2 — Recover and harden the imagery streaming vertical slice

**Goal:** prove the existing provider/cache/upload/virtual-texture chain works
as one buildable path before replacing geometry.

- Make provider, byte cache, decode cache, scheduler, GL upload queue,
  physical texture layers, and indirection ownership explicit.
- Restore any accidentally orphaned working components from the baseline when
  necessary; do not invent parallel APIs.
- Add a deterministic parent-fallback placeholder/base layer.
- Add bounded residency/upload counters and remove noisy default logs.

**Acceptance:** basic example shows a textured globe; imagery streams, uploads,
and evicts without gray frames; test or instrument parent fallback.

**Proposed commit:** `feat: harden streamed imagery virtual-texture pipeline`

### Phase 3 — Canonical imagery address and page-table contract

**Goal:** eliminate the high-zoom CPU/GPU addressing divergence at its source.

- Define `TileMatrixSet`, `ImageTileKey`, and a versioned virtual-address
  contract shared by selection, residency, upload, and shaders.
- Define camera/patch-local page-table windows with integer origins and
  generation ownership.
- Add GLSL/CPU conformance fixtures at z13, z18, and z21.
- Ensure the shader samples only the snapshot's page-table generation.

**Acceptance:** conformance tests have zero unexpected address mismatch; high
zoom uses parent fallback while child imagery is loading, not gray output.

**Proposed commit:** `feat: unify virtual imagery addressing across CPU and GPU`

### Phase 4 — Build ellipsoid geographic receiver patches

**Goal:** introduce the final production surface without terrain displacement.

- Implement geographic quadtree keys, bounds, ellipsoid patch mesh creation,
  patch-local transforms, and an SSE selector.
- Render selected WGS84 ellipsoid patches camera-relatively.
- Keep the current icosphere solely as a coarse fallback until the patch view
  covers the globe and passes comparison checks.
- Implement neighbour level constraints and a simple skirt/stitch policy.

**Acceptance:** a fully textured ellipsoid can be rendered through geographic
patches at normal and high zoom, with no cracks and no global mesh rebuild.

**Proposed commit:** `feat: render WGS84 geographic quadtree ellipsoid patches`

### Phase 5 — Bind virtual imagery to geographic patches

**Goal:** move high-zoom imagery to the geometry that owns its geographic
coordinates.

- Derive patch-local imagery coordinates from the explicit geographic rectangle
  and source matrix set.
- Request, resolve, and sample virtual imagery per selected patch.
- Validate anti-meridian, Web Mercator latitude limits, source y convention,
  and ancestor fallback.
- Make geographic patches the production imagery path; retain only a
  deliberately limited coarse icosphere fallback while coverage is verified.

**Acceptance:** z13/z18/z21 city presets render correct imagery without
CPU/GPU tile disagreement, gray holes, or precision-dependent coordinate drift.

**Proposed commit:** `feat: sample virtual imagery on geographic globe patches`

### Phase 6 — Scheduler, residency, and eviction policy

**Goal:** make high-zoom loading stable under camera movement.

- Prioritize visible selected patches, then child refinement, then predicted
  motion/prefetch.
- Deduplicate and cancel obsolete requests.
- Pin snapshot-visible pages and ancestor fallbacks.
- Implement measurable LRU/priority eviction and pressure behaviour.
- Test rapid movement, zoom oscillation, cache exhaustion, and request
  cancellation.

**Acceptance:** no invalid layer/page references under stress; metrics show
bounded memory and meaningful eviction decisions.

**Proposed commit:** `feat: add prioritized imagery residency and eviction`

### Phase 7 — Integrate existing terrain as a patch source

**Goal:** preserve and correctly model the terrain already demonstrated by the
project.

- Adapt existing SRTM/elevation code to `TerrainPatchSource`.
- Convert heights to documented WGS84-compatible metres/datum.
- Produce prepared height data, min/max bounds, and geometric error per patch.
- Displace geographic patch vertices and update conservative bounds/normals.

**Acceptance:** existing mountain views reappear on geographic patches; imagery
remains aligned with the displaced terrain; missing terrain falls back cleanly
to the ellipsoid.

**Proposed commit:** `feat: integrate SRTM terrain into geographic patches`

### Phase 8 — Terrain HLOD quality and seams

**Goal:** achieve production terrain refinement rather than global LOD changes.

- Refine terrain by SSE and geometric error independently of imagery.
- Enforce neighbour constraints, skirts/stitches, and parent fallback.
- Cache patch geometry/resources; never rebuild the entire globe for a camera
  distance change.
- Profile culling, draw counts, GPU upload budget, and frame time.

**Acceptance:** smooth local terrain refinement, no cracks, stable frame time,
and no global mesh rebuild during navigation.

**Proposed commit:** `feat: add SSE terrain LOD and seam-safe patch refinement`

### Phase 9 — Retire obsolete fallback code

**Goal:** leave one production surface and one unambiguous data path.

- Remove the icosphere tile-rendering path only after the geographic patch
  globe meets all Phase 5–8 acceptance criteria.
- Remove normalized-sphere authority, temporary diagnostics, and unused
  loaders/caches only after references and tests are updated.
- Keep a coarse global base layer only if it is part of the chosen geographic
  patch renderer, not as a duplicate globe pipeline.

**Acceptance:** one buildable production renderer, no dead compatibility path,
and all baseline scenes plus high-zoom terrain scenes pass.

**Proposed commit:** `refactor: remove superseded icosphere imagery path`

## Decisions intentionally deferred

These are not required to begin and must not expand early phases:

- quantized-mesh/3D Tiles terrain provider support;
- vector map rendering and labels;
- atmospheric scattering, ocean, and photorealistic terrain material system;
- Vulkan/Qt RHI migration;
- multi-source blending, reprojection, and offline package format;
- distributed disk cache policy.

They fit the boundaries above after imagery, patch terrain, and residency are
verified.

## References used for the architecture

- `modern_map_renderer_techniques.md` supplied by the user: progressive
  refinement, spatial tiling, async scheduling, GPU upload queues, virtual
  residency, and temporal coherence.
- Cesium 3D Tiles specification: WGS84/ECEF-oriented spatial hierarchy,
  geometric error, and screen-space-error traversal.
- Cesium Quantized Mesh specification: streamed terrain tile hierarchy and
  terrain geometric-error concepts.

