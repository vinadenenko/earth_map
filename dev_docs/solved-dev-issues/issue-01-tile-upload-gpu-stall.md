# Issue #1: `tile.upload` GPU stall dominating frame time (fps ~30-40 instead of expected)

**Status:** Diagnosed. Root cause confirmed via GPU profiling data and code trace. Fix not yet implemented.

## Symptom

Desktop app (`basic_example`, `qt-test-app`) runs at 30-40 fps with a static/near-static
camera, far below expectations for a simple globe + tile atlas. Frame-time spikes get worse
during camera move/zoom.

Representative frame from the library's own performance monitoring
(`earth_map::PerformanceStats`, see `include/earth_map/renderer/performance_stats.h`,
requires `EARTH_MAP_ENABLE_PERFORMANCE_MONITORING`):

```
[perf] fps=32 cpu=4.11ms gpu=28.848324ms
    tile.upload: 0.03ms cpu / 28.312644ms gpu (0 draws)
    tile.cull: 0.40ms cpu / 0.178310ms gpu (0 draws)
    tile.draw: 3.48ms cpu / 0.357370ms gpu (1 draws)
```

`tile.upload` alone accounts for ~98% of the frame's GPU time (28.3ms of 28.85ms), which is
almost exactly the whole frame budget implied by fps=32 (1000/32 ≈ 31ms).

## Root cause

The signature -- near-zero CPU (0.03ms), huge GPU time (28.3ms), zero draw calls -- is the
textbook symptom of the GPU *waiting*, not *working*. Traced the actual upload path:

- `TileTextureCoordinator::ProcessUploads()` (`src/renderer/texture_atlas/tile_texture_coordinator.cpp:222`)
  pulls up to 5 pending uploads per frame (`ProcessUploads(int max_uploads_per_frame = 5)`,
  `include/earth_map/renderer/texture_atlas/tile_texture_coordinator.h:245`) and calls
  `TileTexturePool::UploadTile()`.
- `TileTexturePool::UploadTile()` (`src/renderer/tile_pool/tile_texture_pool.cpp:98`) writes
  each tile via `glTexSubImage3D` into **one shared `GL_TEXTURE_2D_ARRAY`** (256x256 tiles,
  up to 512 layers -- `kDefaultTileSize`/`kDefaultMaxPoolLayers`,
  `include/earth_map/renderer/texture_atlas/tile_texture_coordinator.h:63-64` -- the whole
  array is ~128MB).
- That same texture array object is bound and sampled by the `tile.draw` zone's draw call,
  later in the *same* frame (`src/renderer/tile_renderer.cpp`).

Per-upload data is tiny -- one 256x256 RGBA tile is 256KB, at most 5 per frame -- so there is
no legitimate bandwidth reason for 28ms. OpenGL does not track read/write dependencies
per-layer on a texture array, only per-object. The moment a new tile is written into layer N
while a draw call from this frame (or a very recently retired one) might still be sampling
from *any* layer of that same object, the driver must resolve the conflict -- either stall the
GPU until the prior read fully retires, or silently duplicate the entire ~128MB array
internally to avoid stalling the CPU. Either path lands on the GPU timeline, exactly where the
`GL_TIME_ELAPSED` query for `tile.upload` measures, with zero CPU cost and zero draws --
matching the observed data exactly.

This also explains the camera-move/zoom spikes without being a separate issue: moving the
camera streams in more new tiles, so more frames hit this same per-upload hazard.

## Not an architecture problem

The chosen approach -- indirection-texture-based virtual texture streaming (tile cache +
loader/worker pool + indirection texture + LRU eviction of physical pages in a texture array)
-- is the correct, modern approach and is implemented correctly for *what pages to stream*.
This bug is in one specific, separately-solved piece that every real virtual-texturing
implementation needs on top of that: **updating the physical page cache without stalling
whatever is currently sampling from it.** This is a well-known trap in virtual texturing
specifically (id Tech's MegaTexture, UE's Virtual Texturing, and similar systems all pair
their streaming logic with double-buffered page pools, staging buffers with explicit fences,
or async transfer/copy queues for exactly this reason). That safeguard is the piece missing
here -- not a flaw in the overall design.

## Proposed fix (not yet implemented)

Stop writing into the same texture object a draw call might still be reading from in the same
or adjacent frame. Options, roughly in order of how real engines typically implement this:

1. Double/triple-buffer the texture array itself -- rotate which physical array receives
   uploads vs. which is bound for sampling this frame, gated by a GPU fence (`glFenceSync`)
   so a write never lands on an array still possibly in flight.
2. Route uploads through a PBO (ideally persistently mapped, `GL_MAP_PERSISTENT_BIT`),
   decoupling the CPU-side write from the texture's read/write timing.
3. Explicit fencing before reusing a layer, deferring the upload a frame if the prior read
   hasn't retired yet.

Needs a short design pass against the existing `TileTexturePool`/`GLUploadQueue` structure
before implementation.

## How this was found

Enabled `EARTH_MAP_ENABLE_PERFORMANCE_MONITORING` (see `earth_map::PerformanceStats` and its
per-zone `FrameZoneTiming` breakdown), read the resulting per-zone cpu/gpu numbers logged from
`basic_example`, and traced the code path behind the one zone with an anomalous cpu/gpu ratio.
