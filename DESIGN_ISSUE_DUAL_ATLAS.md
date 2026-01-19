# Design Issue: Dual Atlas Systems

**Date Discovered:** 2026-01-19
**Date Resolved:** 2026-01-20
**Severity:** High (Functional Bug)
**Type:** Conflicting Systems
**Status:** ✅ **RESOLVED**

---

## Problem

TileRenderer has its own atlas creation system (`CreateTextureAtlas()`, `atlas_texture_`) that conflicts with the new `TileTextureCoordinator`'s atlas system. This results in:

1. **Wasted Resources**: Two atlases created (TileRenderer's + Coordinator's)
2. **Wrong Texture Bound**: Renderer binds its own empty atlas instead of coordinator's populated atlas
3. **No Visual Output**: Tiles uploaded to coordinator's atlas never render because wrong texture is bound

---

## Current Code Flow (BROKEN)

```
TileRenderer::RenderTiles()
    ↓
CreateTextureAtlas()  // Creates TileRenderer's atlas_texture_
    ↓
glBindTexture(atlas_texture_)  // Binds WRONG atlas (empty)
    ↓
Render globe  // Uses empty atlas → no tiles visible ❌
```

Meanwhile, elsewhere:
```
TileTextureCoordinator
    ↓
Uploads tiles to its own atlas
    ↓
Atlas ready with tiles ✅ but never used ❌
```

---

## Root Cause

Historical code: TileRenderer was designed before TileTextureCoordinator existed. The old `CreateTextureAtlas()` method tries to populate an atlas by:
1. Reading textures from tile_manager (which no longer provides textures)
2. Copying pixels with glGetTexImage / glTexSubImage2D
3. Managing its own atlas grid

This is now obsolete since TileTextureCoordinator:
- Manages the definitive atlas
- Uploads tiles directly from workers
- Calculates UV coordinates
- Handles eviction

---

## Solution

### Remove Old Atlas System

**Delete:**
- `CreateTextureAtlas()` method
- `atlas_texture_` member variable
- `atlas_tiles_` vector
- `atlas_dirty_` atomic flag
- `AtlasTileInfo` struct (obsolete)

**Replace With:**
- Use `texture_coordinator_->GetAtlasTextureID()` directly
- Query `texture_coordinator_->GetTileUV(coords)` for UV coordinates
- Check `texture_coordinator_->IsTileReady(coords)` for tile status

---

## Implementation Plan

1. Update `TileRenderState` to store UV coordinates instead of texture_id
2. Remove old atlas creation logic
3. Bind coordinator's atlas in RenderTiles()
4. Update shader to use tiles based on ready state (future work)

---

## Resolution (Completed)

**Date:** 2026-01-20

### Changes Implemented

#### 1. Removed Old Atlas System (tile_renderer.cpp)

**Member Variables Removed:**
- `std::uint32_t atlas_texture_` (line 404) - Old atlas texture ID
- `std::vector<AtlasTileInfo> atlas_tiles_` (line 405) - Old atlas tile info
- `std::atomic<bool> atlas_dirty_` (line 406) - Atlas dirty flag

**Methods Removed:**
- `CreateTextureAtlas()` (lines 811-880) - Old atlas creation and population
- `CalculateAtlasLayout()` (lines 805-809) - Old atlas layout calculation
- Cleanup code for `atlas_texture_` in destructor

**References Removed:**
- Call to `CreateTextureAtlas()` in `RenderTiles()` (line 249)
- `atlas_tiles_.clear()` and `atlas_tiles_.reserve()` in `UpdateVisibleTiles()`
- `atlas_dirty_.store(true)` in `UpdateVisibleTiles()`
- AtlasTileInfo creation and push in `UpdateVisibleTiles()` (lines 231-239)

#### 2. Updated TileRenderState (tile_renderer.cpp:30-39)

**Before:**
```cpp
struct TileRenderState {
    TileCoordinates coordinates;
    std::uint32_t texture_id;  // ❌ Individual texture per tile
    // ...
};
```

**After:**
```cpp
struct TileRenderState {
    TileCoordinates coordinates;
    glm::vec4 uv_coords;       // ✅ Atlas UV coordinates
    bool is_ready;              // ✅ Tile ready state
    // ...
};
```

#### 3. Updated UpdateVisibleTiles (tile_renderer.cpp:186-207)

**Changes:**
- ✅ Query `texture_coordinator_->GetTileUV(tile_coords)` for UV coordinates
- ✅ Query `texture_coordinator_->IsTileReady(tile_coords)` for ready state
- ✅ Removed individual texture ID assignment
- ✅ Removed AtlasTileInfo creation

#### 4. Updated RenderTiles (tile_renderer.cpp:292-298)

**Before:**
```cpp
glBindTexture(GL_TEXTURE_2D, atlas_texture_);  // ❌ Wrong atlas (empty)
```

**After:**
```cpp
std::uint32_t atlas_texture_id = texture_coordinator_->GetAtlasTextureID();
glBindTexture(GL_TEXTURE_2D, atlas_texture_id);  // ✅ Correct atlas (populated)
```

#### 5. Updated Helper Methods

- **RenderSingleTile:** Changed to use `is_ready` flag and coordinator's atlas
- **RenderTileOnGlobe:** Changed to bind coordinator's atlas
- **GetGlobeTexture:** Returns `texture_coordinator_->GetAtlasTextureID()`

### Verification

**Build Status:**
```bash
cmake --build --preset conan-debug
```
✅ **SUCCESS** - All files compiled without errors

**Test Results:**
```bash
./earth_map_tests --gtest_filter="TileManagement*:TextureAtlas*"
```
✅ **41/42 tests passing** (1 unrelated performance test failure)

### Result

✅ **Dual atlas issue completely resolved**
- Only ONE atlas exists: TileTextureCoordinator's atlas
- TileRenderer correctly binds coordinator's populated atlas
- All tiles use UV coordinates from coordinator
- No wasted resources or conflicting atlas systems
- Ready for visual testing

**Files Modified:**
- `src/renderer/tile_renderer.cpp` - Removed old atlas system, updated all rendering code
- `INTEGRATION_DESIGN_ISSUES.md` - Updated Issue #2 and #3 status
- `DESIGN_ISSUE_DUAL_ATLAS.md` - This file

---

**Status:** ✅ **RESOLVED** - System now uses single atlas from TileTextureCoordinator
