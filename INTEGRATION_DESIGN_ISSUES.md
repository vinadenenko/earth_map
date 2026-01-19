# Integration Design Issues - TileTextureCoordinator Integration

**Date:** 2026-01-19
**Context:** Integration of new lock-free TileTextureCoordinator to replace mutex-based TileTextureManager

## Issue 1: TileManager Has Texture Management Responsibilities

**Severity:** High (Architectural)
**Type:** Separation of Concerns Violation
**Status:** ✅ **RESOLVED** (2026-01-19)

### Problem

`TileManager` currently has methods for texture management:
- `GetTileTexture()`
- `LoadTileTextureAsync()`
- `SetTextureManager()`
- `InitializeWithTextureManager()`

This violates separation of concerns:
- **TileManager** should handle: Tile data acquisition (cache + network), coordinate logic
- **TileTextureCoordinator** should handle: ALL GPU texture operations (loading, decoding, atlas management)

### Current State

```cpp
// TileManager interface (WRONG - mixes concerns)
class TileManager {
    virtual std::uint32_t GetTileTexture(const TileCoordinates& coordinates) const = 0;
    virtual std::future<bool> LoadTileTextureAsync(...) = 0;
    virtual void SetTextureManager(std::shared_ptr<TileTextureManager> texture_manager) = 0;
    // ... tile data methods
};
```

### Root Cause

Historical design where TileManager owned texture operations. The new architecture (TileTextureCoordinator) was added alongside the old system without refactoring TileManager.

### Impact

1. **Confusing API**: Callers unclear whether to use TileManager or TileTextureCoordinator for textures
2. **Coupling**: TileManager coupled to texture system it shouldn't know about
3. **Dead Code**: After integration, these methods become unused/deprecated
4. **Testing Complexity**: Must mock texture manager for tile manager tests

### Recommended Solution

**Phase 1 (Current):** Mark methods as deprecated, make them no-ops
```cpp
[[deprecated("Use TileTextureCoordinator directly")]]
virtual std::uint32_t GetTileTexture(const TileCoordinates& coordinates) const = 0;
```

**Phase 2 (Future Refactor):** Remove texture methods entirely
```cpp
// TileManager interface (CORRECT - single responsibility)
class TileManager {
    // Tile data methods only
    virtual std::vector<TileCoordinates> GetTilesInBounds(...) = 0;
    virtual TileData GetTileData(const TileCoordinates& coords) = 0;
    // NO texture methods
};
```

**Phase 3:** Update callers to use TileTextureCoordinator for ALL texture operations

### Resolution (Completed)

**Date:** 2026-01-19
**Approach:** Phase 2 (Direct Refactor) - Removed texture methods entirely

**Changes Made:**

1. **Interface (tile_manager.h):**
   - ❌ Removed `GetTileTexture()`
   - ❌ Removed `LoadTileTextureAsync()`
   - ❌ Removed `SetTextureManager()`
   - ❌ Removed `InitializeWithTextureManager()`
   - ❌ Removed forward declaration of `TileTextureManager`
   - ❌ Removed `TileTextureCallback` typedef

2. **Implementation (tile_manager.cpp):**
   - ❌ Removed all method implementations
   - ❌ Removed `texture_manager_` member variable
   - ❌ Removed `#include "earth_map/renderer/tile_texture_manager.h"`

3. **Call Sites:**
   - ✅ TileRenderer updated to use TileTextureCoordinator directly
   - ✅ TriggerTileLoading() removed (dead code - Issue #4)

**Result:**
- ✅ TileManager now has ONLY tile data responsibilities
- ✅ Clear separation: TileManager (data) vs TileTextureCoordinator (rendering)
- ✅ Build successful
- ✅ 21/22 tests passing (1 unrelated performance test failure)

---

## Issue 2: TileRenderer Still Uses Individual Texture IDs

**Severity:** Medium (Performance)
**Type:** Atlas Integration Incomplete
**Status:** ✅ **RESOLVED** (2026-01-20)

### Problem

`TileRenderState` stores individual `texture_id` per tile:
```cpp
struct TileRenderState {
    std::uint32_t texture_id;  // ← Should be removed, all tiles use atlas
    // ...
};
```

With texture atlas, all tiles share ONE atlas texture. UV coordinates differentiate tiles.

### Resolution (Completed)

**Date:** 2026-01-20

**Changes Made:**

1. **Updated TileRenderState structure** (tile_renderer.cpp:30-39):
   - ❌ Removed `std::uint32_t texture_id` field
   - ✅ Added `glm::vec4 uv_coords` - Atlas UV coordinates from coordinator
   - ✅ Added `bool is_ready` - Whether tile texture is ready in atlas

2. **Updated UpdateVisibleTiles** (tile_renderer.cpp:186-207):
   - ✅ Query `texture_coordinator_->GetTileUV(coords)` for UV coordinates
   - ✅ Query `texture_coordinator_->IsTileReady(coords)` for ready state
   - ✅ Removed individual texture ID assignment per tile

3. **Updated RenderTiles** (tile_renderer.cpp:292-298):
   - ✅ Bind coordinator's atlas once: `texture_coordinator_->GetAtlasTextureID()`
   - ✅ Single texture bind per frame (all tiles use same atlas)

4. **Updated helper methods**:
   - ✅ RenderSingleTile: Changed to use `is_ready` and coordinator's atlas
   - ✅ RenderTileOnGlobe: Changed to use coordinator's atlas
   - ✅ GetGlobeTexture: Returns coordinator's atlas texture ID

**Result:**
- ✅ All tiles now use single shared atlas texture
- ✅ UV coordinates stored per tile for atlas sampling
- ✅ Build successful, tests passing (41/42)
- ✅ Ready for visual testing

---

## Issue 3: Shader Needs Atlas UV Support

**Severity:** Medium (Functional)
**Type:** Missing Implementation
**Status:** ⏳ **PARTIALLY RESOLVED** (2026-01-20)

### Problem

Current fragment shader calculates tile texture sampling based on world position, but doesn't use atlas UV coordinates from TileTextureCoordinator.

### Current State

**Shader:** The fragment shader (tile_renderer.cpp:448-503) calculates UV coordinates dynamically:
```glsl
// Calculates tile position dynamically from world coordinates
vec3 geo = worldToGeo(normalize(FragPos));
vec2 tile = geoToTile(geo.xy, zoom);
vec2 atlasUV = (atlasPos + tileFrac) * tileSize;
vec4 texColor = texture(uTileTexture, atlasUV);
```

### Resolution (Partially Completed)

**Date:** 2026-01-20

**Changes Made:**

1. **TileRenderer binds coordinator's atlas** (tile_renderer.cpp:292-298):
   - ✅ Correctly binds `texture_coordinator_->GetAtlasTextureID()`
   - ✅ All rendering code updated to use coordinator's atlas

2. **TileRenderState stores UV coordinates** (tile_renderer.cpp:30-39):
   - ✅ `glm::vec4 uv_coords` field added
   - ✅ Populated from `texture_coordinator_->GetTileUV()`

**Limitation:**

The shader still uses dynamic UV calculation instead of the pre-computed UV coordinates from `TileRenderState`. This works for basic rendering but has limitations:
- Assumes tiles are arranged in a grid pattern in the atlas
- May not correctly map to coordinator's actual atlas slot allocation
- Cannot handle non-contiguous atlas layouts

**Future Work:**

For full per-tile UV support, the shader needs to:
1. Accept per-tile UV coordinates (either as vertex attributes or uniform buffer)
2. Use those coordinates directly for atlas sampling
3. Handle cases where tiles are not yet loaded

This requires either:
- Per-tile rendering (one draw call per tile with different UV uniform)
- Uniform buffer with UV lookup table indexed by tile coordinates
- Vertex attributes for UV coordinates

**Result:**
- ✅ Basic shader functionality preserved
- ✅ Binds correct atlas texture from coordinator
- ⚠️ Dynamic UV calculation may not perfectly align with atlas slots
- ✅ Ready for visual testing to identify any rendering issues

---

## Issue 4: TriggerTileLoading() Method Still Present

**Severity:** Low (Code Cleanup)
**Type:** Dead Code
**Status:** ✅ **RESOLVED** (2026-01-19 - as part of Issue #1 fix)

### Problem

`TileRenderer::TriggerTileLoading()` is called in UpdateVisibleTiles but is no longer needed with TileTextureCoordinator (which automatically loads via RequestTiles).

### Resolution (Completed)

**Date:** 2026-01-19

**Changes Made:**
- ❌ Removed `TriggerTileLoading()` method definition (line 1264-1280)
- ❌ Removed call to `TriggerTileLoading(tile_coords)` in UpdateVisibleTiles (line 237)
- ✅ Added comment noting tile loading is now handled by TileTextureCoordinator::RequestTiles()

**Result:**
- ✅ Dead code removed
- ✅ Cleaner codebase
- ✅ All tile loading now goes through TileTextureCoordinator

---

## Summary

| Issue | Severity | Status | Date Resolved |
|-------|----------|--------|---------------|
| #1: TileManager texture methods | High | ✅ **RESOLVED** | 2026-01-19 |
| #2: Individual texture IDs per tile | Medium | ✅ **RESOLVED** | 2026-01-20 |
| #3: Shader atlas UV support | Medium | ⏳ **PARTIAL** | 2026-01-20 |
| #4: Dead TriggerTileLoading code | Low | ✅ **RESOLVED** | 2026-01-19 |
| Dual Atlas Systems (discovered) | High | ✅ **RESOLVED** | 2026-01-20 |

**Completed (2026-01-19):**
1. ✅ Complete basic integration (TileTextureCoordinator wired to TileRenderer)
2. ✅ Build and test current integration (21/22 tests passing)
3. ✅ Remove TileManager texture methods entirely (Issue #1)
4. ✅ Remove TriggerTileLoading() dead code (Issue #4)

**Completed (2026-01-20):**
1. ✅ Discovered and documented dual atlas systems issue
2. ✅ Removed old atlas system (CreateTextureAtlas, atlas_texture_, atlas_tiles_, atlas_dirty_)
3. ✅ Updated TileRenderState to use UV coordinates and is_ready flag (Issue #2)
4. ✅ Updated UpdateVisibleTiles to query coordinator for UV coords and ready state
5. ✅ Updated RenderTiles to bind coordinator's atlas texture
6. ✅ Updated all helper methods (RenderSingleTile, RenderTileOnGlobe, GetGlobeTexture)
7. ✅ Build successful, tests passing (41/42)
8. ✅ Ready for visual testing

**Remaining Work:**
1. ⚠️ Shader UV integration (Issue #3 - Partial): Shader still uses dynamic UV calculation
   - Current: Works for basic rendering, may have tile mapping issues
   - Future: Full per-tile UV support via vertex attributes or uniform buffer
2. 📋 Visual testing by user to verify atlas rendering works correctly

---

**Author:** Claude (Anthropic AI)
**Date:** 2026-01-19
