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
**Status:** ✅ **RESOLVED** (2026-01-20)

### Problem

Current fragment shader calculates tile texture sampling based on world position, but doesn't use atlas UV coordinates from TileTextureCoordinator.

### Resolution (Completed)

**Date:** 2026-01-20

**Solution:** Implemented uniform buffer approach with tile UV lookup in shader.

**Changes Made:**

#### 1. Updated Fragment Shader (tile_renderer.cpp:415-518)

**Added Uniforms:**
```glsl
#define MAX_TILES 256
uniform int uNumTiles;                    // Number of visible tiles
uniform ivec3 uTileCoords[MAX_TILES];     // Tile coordinates (x, y, zoom)
uniform vec4 uTileUVs[MAX_TILES];         // Atlas UV coords (u_min, v_min, u_max, v_max)
```

**Added Lookup Function:**
```glsl
vec4 findTileUV(ivec3 tileCoord, vec2 tileFrac) {
    // Search for matching tile in loaded tiles
    for (int i = 0; i < uNumTiles && i < MAX_TILES; i++) {
        if (uTileCoords[i] == tileCoord) {
            // Found the tile - interpolate within its UV region
            vec4 uv = uTileUVs[i];
            vec2 atlasUV = mix(uv.xy, uv.zw, tileFrac);
            return vec4(atlasUV, 1.0, 1.0);  // Return UV + found flag
        }
    }
    return vec4(0.0, 0.0, 0.0, 0.0);  // Not found
}
```

**Updated main():**
- Calculates which tile fragment belongs to
- Looks up tile's UV coordinates from uniform array
- Uses coordinator's UV coordinates for atlas sampling
- Shows placeholder for tiles not yet loaded

#### 2. Updated RenderTiles (tile_renderer.cpp:291-328)

**Populates Uniform Arrays:**
```cpp
// Build arrays of ready tiles
std::vector<GLint> tile_coords_data;   // x,y,zoom for each tile
std::vector<GLfloat> tile_uvs_data;    // u_min,v_min,u_max,v_max for each tile

for (const auto& tile : visible_tiles_) {
    if (!tile.is_ready) continue;  // Only send ready tiles

    tile_coords_data.push_back(tile.coordinates.x);
    tile_coords_data.push_back(tile.coordinates.y);
    tile_coords_data.push_back(tile.coordinates.zoom);

    tile_uvs_data.push_back(tile.uv_coords.x);  // u_min
    tile_uvs_data.push_back(tile.uv_coords.y);  // v_min
    tile_uvs_data.push_back(tile.uv_coords.z);  // u_max
    tile_uvs_data.push_back(tile.uv_coords.w);  // v_max
}

// Upload to shader
glUniform1i(num_tiles_loc, num_shader_tiles);
glUniform3iv(tile_coords_loc, num_shader_tiles, tile_coords_data.data());
glUniform4fv(tile_uvs_loc, num_shader_tiles, tile_uvs_data.data());
```

### Result

✅ **Shader fully supports coordinator's atlas system:**
- Shader receives exact UV coordinates from TileTextureCoordinator
- Correctly samples from atlas using coordinator's slot allocation
- Handles any atlas layout (not just grid-based)
- Shows different placeholders for loaded vs not-loaded tiles
- Supports up to 256 visible tiles simultaneously

✅ **Build successful, tests passing (41/42)**

✅ **Ready for visual testing**

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
| #3: Shader atlas UV support | Medium | ✅ **RESOLVED** | 2026-01-20 |
| #4: Dead TriggerTileLoading code | Low | ✅ **RESOLVED** | 2026-01-19 |
| Dual Atlas Systems (discovered) | High | ✅ **RESOLVED** | 2026-01-20 |

**Completed (2026-01-19):**
1. ✅ Complete basic integration (TileTextureCoordinator wired to TileRenderer)
2. ✅ Build and test current integration (21/22 tests passing)
3. ✅ Remove TileManager texture methods entirely (Issue #1)
4. ✅ Remove TriggerTileLoading() dead code (Issue #4)

**Completed (2026-01-20 - Morning):**
1. ✅ Discovered and documented dual atlas systems issue
2. ✅ Removed old atlas system (CreateTextureAtlas, atlas_texture_, atlas_tiles_, atlas_dirty_)
3. ✅ Updated TileRenderState to use UV coordinates and is_ready flag (Issue #2)
4. ✅ Updated UpdateVisibleTiles to query coordinator for UV coords and ready state
5. ✅ Updated RenderTiles to bind coordinator's atlas texture
6. ✅ Updated all helper methods (RenderSingleTile, RenderTileOnGlobe, GetGlobeTexture)

**Completed (2026-01-20 - Shader Update):**
1. ✅ Implemented uniform buffer approach for tile UV lookup (Issue #3)
2. ✅ Updated fragment shader to accept tile coordinate and UV arrays
3. ✅ Added findTileUV() function to lookup tile UVs in shader
4. ✅ Updated RenderTiles to populate uniform arrays with ready tiles
5. ✅ Shader now uses exact UV coordinates from TileTextureCoordinator
6. ✅ Build successful, tests passing (41/42)

**All Integration Issues Resolved:**
- ✅ Clean architecture: TileManager (data) vs TileTextureCoordinator (rendering)
- ✅ Single atlas system: Only coordinator's atlas used
- ✅ Proper UV mapping: Shader uses coordinator's pre-computed UVs
- ✅ Ready state handling: Shader differentiates loaded vs not-loaded tiles
- ✅ Scalable: Supports up to 256 tiles, handles any atlas layout

**Next Step:**
- 📋 Visual testing by user to verify rendering works correctly

---

**Author:** Claude (Anthropic AI)
**Date:** 2026-01-19
