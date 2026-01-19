# Integration Design Issues - TileTextureCoordinator Integration

**Date:** 2026-01-19
**Context:** Integration of new lock-free TileTextureCoordinator to replace mutex-based TileTextureManager

## Issue 1: TileManager Has Texture Management Responsibilities

**Severity:** High (Architectural)
**Type:** Separation of Concerns Violation

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

### Workaround (Current Implementation)

For now, TileManager texture methods return default values:
- `GetTileTexture()` → returns 0 (no texture)
- `LoadTileTextureAsync()` → returns failed future
- `SetTextureManager()` → no-op
- `InitializeWithTextureManager()` → delegates to regular Initialize()

TileRenderer now uses TileTextureCoordinator directly, bypassing TileManager's texture methods.

---

## Issue 2: TileRenderer Still Uses Individual Texture IDs

**Severity:** Medium (Performance)
**Type:** Atlas Integration Incomplete

### Problem

`TileRenderState` stores individual `texture_id` per tile:
```cpp
struct TileRenderState {
    std::uint32_t texture_id;  // ← Should be removed, all tiles use atlas
    // ...
};
```

With texture atlas, all tiles share ONE atlas texture. UV coordinates differentiate tiles.

### Current State

- `UpdateVisibleTiles()` assigns `atlas_texture_id` to each tile
- Shader needs UV coordinates, not individual texture IDs
- Inefficient: binding same texture multiple times

### Recommended Solution

```cpp
struct TileRenderState {
    TileCoordinates coordinates;
    glm::vec4 uv_coords;  // ← From TileTextureCoordinator::GetTileUV()
    bool is_ready;        // ← From TileTextureCoordinator::IsTileReady()
    // NO texture_id - renderer binds atlas once per frame
};
```

Rendering flow:
```cpp
// Bind atlas once
glBindTexture(GL_TEXTURE_2D, texture_coordinator_->GetAtlasTextureID());

for (auto& tile : visible_tiles_) {
    if (tile.is_ready) {
        // Pass UV coordinates to shader
        shader.setVec4("uTileUV", tile.uv_coords);
        // Render tile geometry
    }
}
```

---

## Issue 3: Shader Needs Atlas UV Support

**Severity:** Medium (Functional)
**Type:** Missing Implementation

### Problem

Current fragment shader calculates tile texture sampling based on world position, but doesn't use atlas UV coordinates from TileTextureCoordinator.

### Current Shader

```glsl
// Calculates tile position dynamically (doesn't use atlas UVs)
vec2 tile = geoToTile(geo.xy, zoom);
vec2 atlasUV = (atlasPos + tileFrac) * tileSize;
vec4 texColor = texture(uTileTexture, atlasUV);
```

### Recommended Solution

Pass UV coordinates from CPU (pre-computed by TileTextureCoordinator):

```glsl
// Vertex shader
layout (location = 3) in vec4 aTileUV;  // (u_min, v_min, u_max, v_max)
out vec4 TileUV;

void main() {
    TileUV = aTileUV;
    // ...
}

// Fragment shader
in vec4 TileUV;

void main() {
    // Interpolate within tile's UV region
    vec2 atlasUV = mix(TileUV.xy, TileUV.zw, tileFrac);
    vec4 texColor = texture(uTileTexture, atlasUV);
    // ...
}
```

---

## Issue 4: TriggerTileLoading() Method Still Present

**Severity:** Low (Code Cleanup)
**Type:** Dead Code

### Problem

`TileRenderer::TriggerTileLoading()` is called in UpdateVisibleTiles but is no longer needed with TileTextureCoordinator (which automatically loads via RequestTiles).

### Recommended Solution

Remove `TriggerTileLoading()` calls and method definition.

---

## Summary

| Issue | Severity | Status | Fix Priority |
|-------|----------|--------|--------------|
| TileManager texture methods | High | Workaround | Phase 2 |
| Individual texture IDs per tile | Medium | TODO | Phase 1 |
| Shader atlas UV support | Medium | TODO | Phase 1 |
| Dead TriggerTileLoading code | Low | TODO | Phase 1 |

**Next Steps:**
1. ✅ Complete basic integration (TileTextureCoordinator wired to TileRenderer)
2. ⏳ Build and test current integration
3. 🔄 Update shader to use atlas UVs (Phase 1)
4. 🔄 Remove TileRenderState::texture_id (Phase 1)
5. 📋 Deprecate TileManager texture methods (Phase 2)
6. 📋 Remove TileManager texture methods entirely (Phase 3)

---

**Author:** Claude (Anthropic AI)
**Date:** 2026-01-19
