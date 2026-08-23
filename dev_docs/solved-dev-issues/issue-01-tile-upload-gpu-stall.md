# Issue #1: `tile.upload` GPU stall dominating frame time (fps ~30-40 instead of expected)

**Status:** Fencing fix implemented (option 3 below) and measured against a real before/after
`perf_flight.log` comparison -- **no measured improvement**. When an upload actually happens,
GPU cost is unchanged (median 16.7ms before vs 18.2ms after, filtering to only frames where a
real upload occurred). The fencing approach assumes the cost is a timing hazard the GPU
resolves once a prior read is confirmed done; the data doesn't support that -- waiting for the
fence doesn't make the write cheap. Root cause needs re-examination; see "Open question" below.

**IMPORTANT CAVEAT ON ALL NUMBERS IN THIS DOCUMENT SO FAR:** every measurement here -- the
original 28ms discovery, the benchmark numbers, and this fencing before/after comparison -- was
taken on a Linux VM guest with **no GPU passthrough**, confirmed via `GL_RENDERER: llvmpipe
(LLVM 15.0.7, 256 bits)` (a pure software rasterizer, no real GPU involved at all). The host has
a real GPU (RTX 3070 Ti), but the VM currently can't reach it (see the general GL/mesa
`RUNPATH` issue noted elsewhere in this session -- possibly related, not confirmed). This means:
the specific "fencing doesn't help" conclusion above is only established on software rendering.
It is entirely possible the underlying mechanism is different on real hardware (a genuine
async GPU-pipeline hazard fencing *would* fix) versus whatever llvmpipe is actually doing here
(more likely a size/re-tiling cost fencing *can't* fix, per "Open question" below). **Real
hardware numbers are the actual decisive test for whether this fix does anything** -- treat
everything above as VM-only evidence until re-measured on a real GPU.

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

## Fix implemented: option 3, explicit fencing

Considered, in order of how real engines typically implement this:

1. Double/triple-buffer the texture array itself -- rejected. Every resident layer would need
   to exist in every buffered copy for sampling to stay correct (the indirection texture maps
   a tile to one specific layer index, not "whichever generation is current"), which means
   duplicating the *entire resident set* on every swap -- exactly the expensive copy this fix
   is trying to avoid, and a poor fit for a long-lived LRU cache where most layers are stable
   across many frames and only a handful change per frame.
2. PBO-based upload -- would help decouple the CPU-side write from GL's copy timing, but does
   not by itself resolve the GPU-side hazard: OpenGL still only tracks the read/write conflict
   at the whole-texture-object level, not per layer, so the underlying stall/copy is unchanged
   regardless of how the CPU stages the source data.
3. **Explicit fencing (implemented).** OpenGL's hazard tracking is already whole-object, so a
   whole-object synchronization point is the natural match, and it needs no new resources
   (no second array, no PBOs) and no indirection/shader changes.

### How it works

- `TileTexturePool::MarkSampled()` (`tile_texture_pool.h/.cpp`) creates a `glFenceSync` right
  after a draw call that sampled the array. `IsSafeToUpload()` does a single non-blocking
  `glGetSynciv(..., GL_SYNC_STATUS, ...)` poll of that fence.
- `TileRenderer::RenderTiles()` calls `texture_coordinator_->MarkArraySampled()` immediately
  after its `glDrawElements` call (the one draw call that samples the pool's array).
- `TileTextureCoordinator::ProcessUploads()` checks `tile_pool_->IsSafeToUpload()` first and
  returns immediately (queue untouched) if the GPU hasn't yet confirmed the last sampling
  draw is finished -- deferring the *entire* batch by a frame or two, not just one layer,
  since the hazard is object-wide.

Net effect: `UploadTile()`'s `glTexSubImage3D` only ever runs once the GPU has already
confirmed it's done reading the array, so the driver has no reason to stall or duplicate
anything. Cost: uploads can be delayed by roughly a frame under contention -- imperceptible,
since tiles already stream in asynchronously with LRU/fade-in behavior.

## How this was found

Enabled `EARTH_MAP_ENABLE_PERFORMANCE_MONITORING` (see `earth_map::PerformanceStats` and its
per-zone `FrameZoneTiming` breakdown), read the resulting per-zone cpu/gpu numbers logged from
`basic_example`, and traced the code path behind the one zone with an anomalous cpu/gpu ratio.

## Measured result: fencing did not help

Compared real `perf_flight.log` runs from `basic_example`'s scripted flight, before and after
the fencing fix, filtering to only frames where a real upload actually occurred
(`tile.upload`'s `cpu` field > 0.1ms -- excludes frames where the queue was empty or the
upload was deferred, which were already cheap in both logs and aren't the thing being fixed):

```
BEFORE FIX: real uploads = 966/1886 frames (51.2%)
  real_upload_gpu_ms   mean=17.324ms   median=16.738ms   max=166.875ms

AFTER FIX:  real uploads = 992/1673 frames (59.3%)
  real_upload_gpu_ms   mean=20.276ms   median=18.166ms   max=149.889ms
```

No improvement -- if anything slightly worse, though within noise on a software-rendered VM.
When an upload actually runs, its GPU cost is unaffected by whether the fence confirmed the
prior sampling read had finished first.

## Timing-hazard theory: now directly disproven (on the VM), not just unconfirmed

Added a log line at the exact point `ProcessUploads()` defers because `IsSafeToUpload()` says
the array might still be in use:

```cpp
if (!tile_pool_->IsSafeToUpload()) {
    spdlog::info("[issue-01] tile.upload deferred: array still in use by prior sampling draw");
    return;
}
```

Filter with `grep "\[issue-01\]"`. Result: **this line never fires.** `IsSafeToUpload()` returns
`true` every single time it's checked -- the fence from the previous frame's draw is always
already signaled by the time the next frame's `ProcessUploads()` checks it. There is never a
moment, in this app on this VM, where the GPU might still be reading the array from a prior
draw by the time an upload is attempted. (Ruled out the alternative explanation -- that
`MarkArraySampled()` simply isn't being called, so there's nothing to wait for rather than
"already done" -- by inspection: `RenderTiles()` cannot draw tiles at all without a valid
`texture_coordinator_`, so the call site is reached whenever there's anything to sample.)

Combined with the before/after measurement above (upload cost unchanged whether or not the
fence gates it), this closes the loop: **the cost has nothing to do with a prior read still
being in flight.** There is no race being won or lost here, so fencing was never going to help,
on this environment.

## Leading theory now: cost tied to array *size*, not timing

`glTexSubImage3D` on a large (512-layer, 128MB) array that has already been used for sampling
may need to re-tile/re-validate the driver's internal representation on *every* update, as an
inherent per-call cost tied to the array's size -- particularly plausible for this
environment's software rasterizer (llvmpipe). If so, `WhileSampled/512` should cost
substantially more than `WhileSampled/32` in the benchmark below, independent of contention.

Next step: run `tests/performance/tile_upload_stall_benchmark.cpp`'s array-size sweep (`/8`,
`/32`, `/128`, `/512`, both `WhileSampled` and `WithoutContention`) and read the actual numbers
across sizes. Not yet done as of this writing.

**Caveat repeated from above, doubly important now:** all of this -- the disproven timing
theory included -- was established on the software-rendered VM. Real GPU hardware may behave
completely differently: a real discrete/integrated GPU's driver has genuine deep pipelining
that llvmpipe may not meaningfully replicate, so a real timing hazard (and a real fix from
fencing) is still entirely possible there. Don't treat the VM's disproof as portable to
hardware without re-running the same `[issue-01]` log check there.

## How to verify a fix (tooling, still valid regardless of the above)

Two independent tools exist for a before/after comparison. Baseline numbers (before the
fencing fix) are in the Symptom section above and in `perf_flight.log` from that run:

1. **Isolated benchmark**: `tests/performance/tile_upload_stall_benchmark.cpp`, built via the
   `with_benchmarks` conan option (`conan install . -o with_benchmarks=True --build=missing`,
   requires its own `EARTH_MAP_BUILD_BENCHMARKS` CMake option, separate from
   `EARTH_MAP_BUILD_TESTS` since it needs a real GL context). Reproduces the exact hazard in
   isolation (`BM_TileUpload_WhileSampled`) against a control with no contention
   (`BM_TileUpload_WithoutContention`) -- the gap between the two *is* this bug. Run the
   `earth_map_benchmarks` executable directly; it is not wired into CTest (real GPU timing is
   too hardware/driver-dependent for a pass/fail gate).
2. **Real-app scenario**: press `P` in `basic_example` to run the scripted camera-flight
   scenario (dives across several Armenia locations at alternating zoom levels, matching the
   real move/zoom trigger for this bug). Logs every frame's `PerformanceStats` to
   `perf_flight.log`. Input is locked out while it runs so two runs are actually comparable.
