# Interface Issues and Design Considerations

This document tracks interface design issues discovered during the Texture Atlas System implementation. These should be reviewed and addressed in future iterations.

## 1. TileLoader - Synchronous LoadTile Return Type

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

**Solution** Since std::optional would miss important metadata from TileLoadResult, skip it. It is ok to return TileLoadResult.

---

## 2. TileRenderer - SetTileManager Raw Pointer

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

**Solution** Defer this for later.

---

## 3. Error Handling - Inconsistent Patterns

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

**Solution** Defer it
---

## 4. Thread Safety - Interface Documentation

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

**Solution** Defer it.
---

## 5. TileCoordinates - Validation

**File:** `include/earth_map/math/tile_mathematics.h`

**Issue:** `TileCoordinates` has `IsValid()` method, but many interfaces don't validate coordinates before use.

**Consideration:**
- Should invalid coordinates cause exceptions or be handled gracefully?
- Should interfaces validate inputs or assume callers provide valid data?

**Recommendation:**
- Use precondition checks (assertions in debug, explicit checks in release)
- Document preconditions clearly in interface comments
- Consider using `gsl::Expects()` for preconditions

**Solution** Defer it.
---

## Summary

Remaining issues are minor and relate to documentation and consistency. The core architecture is sound.

**Completed Fixes:**
- ✅ TileCache naming: Changed `Retrieve()` → `Get()` with `std::optional<TileData>`
- ✅ TileCache naming: Changed `Store()` → `Put()`
- ✅ Separation of concerns: TileTextureCoordinator now handles texture loading (TileManager no longer has texture methods)

**Remaining Recommendations:**
1. **Ownership Clarity**: Use references or smart pointers instead of raw pointers (deferred)
2. **Error Handling**: Establish consistent patterns across codebase (deferred)
3. **Documentation**: Add thread safety and precondition documentation (deferred)

None of the remaining issues block the current implementation. They should be addressed in a future refactoring pass for polish and maintainability.
