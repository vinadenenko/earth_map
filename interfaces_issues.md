# Interface Issues and Design Considerations

This document tracks interface design issues discovered during the Texture Atlas System implementation. These should be reviewed and addressed in future iterations.

## 1. TileCache Interface - Retrieve vs Get Method Naming

**File:** `include/earth_map/data/tile_cache.h`

**Issue:** The `TileCache` interface uses `Retrieve()` which returns `std::shared_ptr<TileData>`, but a more conventional pattern would be `Get()` with an out-parameter or optional return.

**Current Signature:**
```cpp
virtual std::shared_ptr<TileData> Retrieve(const TileCoordinates& coordinates) = 0;
```

**Consideration:**
- Most cache interfaces use `Get()` or `TryGet()` nomenclature
- Returning `nullptr` for cache miss is fine, but could be more explicit with `std::optional<TileData>` or bool return + out-parameter
- The shared_ptr adds ref-counting overhead that may not be needed

**Recommendation:** Consider renaming to `Get()` for consistency with standard cache naming, or use `std::optional<TileData>` to make cache miss more explicit.

---

## 2. TileCache - Store vs Put Method Naming

**File:** `include/earth_map/data/tile_cache.h`

**Issue:** The `TileCache` interface uses `Store()` but cache interfaces typically use `Put()` or `Set()`.

**Current Signature:**
```cpp
virtual bool Store(const TileData& tile_data) = 0;
```

**Consideration:**
- `Put()` is more common in cache APIs (e.g., Guava Cache, Caffeine, etc.)
- `Store()` is not wrong, but less conventional

**Recommendation:** Consider renaming to `Put()` for better alignment with industry standard cache APIs.

---

## 3. TileLoader - Synchronous LoadTile Return Type

**File:** `include/earth_map/data/tile_loader.h`

**Issue:** `LoadTile()` returns a complete `TileLoadResult` struct by value, which includes vectors and strings that get copied.

**Current Signature:**
```cpp
virtual TileLoadResult LoadTile(const TileCoordinates& coordinates,
                               const std::string& provider_name = "") = 0;
```

**Consideration:**
- `TileLoadResult` contains `std::shared_ptr<TileData>` and `std::string` members
- Could be more efficient to return by const reference or use move semantics explicitly
- Or return `std::optional<TileData>` directly and use exceptions for errors

**Recommendation:** Consider returning `std::optional<TileData>` or using move semantics more explicitly (though modern compilers should optimize the current approach with RVO).

---

## 4. TileRenderer - SetTileManager Raw Pointer

**File:** `include/earth_map/renderer/tile_renderer.h`

**Issue:** `SetTileManager()` takes a raw pointer, which doesn't communicate ownership.

**Current Signature:**
```cpp
virtual void SetTileManager(TileManager* tile_manager) = 0;
```

**Consideration:**
- Raw pointer doesn't indicate: observer pointer, owning pointer, or required non-null
- Could be `TileManager&` (non-null reference) or `std::shared_ptr<TileManager>` (shared ownership) or `gsl::not_null<TileManager*>` (observer, non-null)

**Recommendation:** Use reference if non-null is required and lifetime is managed externally, or document pointer semantics clearly.

---

## 5. TileManager - LoadTileTextureAsync Signature

**File:** Observed in `src/renderer/tile_renderer.cpp:1247`

**Issue:** The TileManager appears to have a `LoadTileTextureAsync()` method that overlaps with the new TileTextureCoordinator's responsibilities.

**Current Usage:**
```cpp
auto future = tile_manager_->LoadTileTextureAsync(coords, texture_loaded_callback);
```

**Consideration:**
- This creates coupling between TileManager and texture loading
- With the new architecture, TileTextureCoordinator should handle all texture concerns
- TileManager should focus on tile data (cache/network), not texture upload

**Recommendation:**
- Refactor TileManager to remove texture-specific methods
- Have TileManager focus on data acquisition (cache + network)
- Have TileTextureCoordinator handle all GPU texture concerns
- Clear separation of concerns: Data vs Rendering

---

## 6. Error Handling - Inconsistent Patterns

**Files:** Multiple interfaces

**Issue:** Error handling varies across interfaces:
- Some use `bool` return (success/failure)
- Some use exceptions
- Some return `nullptr`
- Some use `std::future<>` without clear error handling

**Examples:**
- `TileCache::Store()` → returns `bool`
- `TileLoader::LoadTile()` → returns struct with `success` field
- `TileCache::Retrieve()` → returns `nullptr` on miss (vs exception)

**Consideration:**
- Mixing patterns makes error handling inconsistent
- Hard to know when to check return values vs catch exceptions

**Recommendation:**
- Establish clear error handling guidelines:
  - Use `std::optional<T>` or `std::expected<T, Error>` for operations that can fail normally
  - Use exceptions only for exceptional errors (programming errors, resource exhaustion)
  - Use `bool` + out-parameter for simple success/failure with data

---

## 7. Thread Safety - Interface Documentation

**Files:** Most interfaces

**Issue:** Thread safety guarantees are not documented in interface comments.

**Consideration:**
- TileCache: Is it thread-safe? Do callers need external synchronization?
- TileLoader: Can LoadTile() be called from multiple threads?
- TileManager: Thread safety unclear

**Recommendation:**
- Add `@threadsafety` documentation tags to all interfaces
- Explicitly state thread safety guarantees
- Document which methods require GL thread vs any thread

---

## 8. TileCoordinates - Validation

**File:** `include/earth_map/math/tile_mathematics.h`

**Issue:** `TileCoordinates` has `IsValid()` method, but many interfaces don't validate coordinates before use.

**Consideration:**
- Should invalid coordinates cause exceptions or be handled gracefully?
- Should interfaces validate inputs or assume callers provide valid data?

**Recommendation:**
- Use precondition checks (assertions in debug, explicit checks in release)
- Document preconditions clearly in interface comments
- Consider using `gsl::Expects()` for preconditions

---

## Summary

Most issues are minor and relate to naming consistency and documentation. The core architecture is sound. Main recommendations:

1. **Naming Consistency**: Align with industry standards (Get/Put for cache, etc.)
2. **Ownership Clarity**: Use references or smart pointers instead of raw pointers
3. **Error Handling**: Establish consistent patterns across codebase
4. **Documentation**: Add thread safety and precondition documentation
5. **Separation of Concerns**: Remove texture logic from TileManager, delegate to TileTextureCoordinator

None of these issues block the current implementation. They should be addressed in a future refactoring pass for polish and maintainability.
