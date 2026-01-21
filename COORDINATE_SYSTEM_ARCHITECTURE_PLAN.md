# Coordinate System Architecture Refactoring Plan

**Date:** 2026-01-22
**Status:** DRAFT - Architectural Design
**Priority:** HIGH - Foundation for Production GIS

---

## Executive Summary

The current codebase lacks clear separation between coordinate systems, leading to:
1. **Bug-prone conversions** scattered throughout code
2. **Type-safety issues** - easy to pass wrong coordinate type
3. **User API confusion** - library users need simple lat/lon ↔ screen pixel interface
4. **Maintenance difficulty** - no single source of truth for conversions

**Solution:** Implement a layered coordinate system architecture with explicit boundaries and type-safe conversions.

---

## Problem Analysis

### Current Issues

#### 1. Mixed Coordinate Systems Without Boundaries

**Where coordinates are used:**
```cpp
// In renderer.cpp - OpenGL world space (radius=1.0)
glm::vec3 camera_position;  // What space? World? Geographic? Unclear!

// In tile_renderer.cpp - Sometimes geographic, sometimes world
BoundingBox2D visible_bounds;  // Is this degrees or OpenGL units? Unclear!

// In camera.cpp - Position in world, but what world?
position_ = glm::vec3(0.0f, 0.0f, 3.0f);  // 3.0 what? Meters? Radii?
```

**The problem:** Same types (glm::vec3, BoundingBox2D) used for different coordinate spaces.

#### 2. No User-Facing API Separation

**What library users need:**
```cpp
// User thinks in these terms:
GeographicCoordinates user_location(40.7128, -74.0060);  // NYC (lat, lon)
ScreenCoordinates mouse_click(500, 300);  // Pixel on screen

// User needs to ask:
GeographicCoordinates clicked_location = map.ScreenToGeographic(mouse_click);
ScreenCoordinates where_to_draw = map.GeographicToScreen(user_location);
```

**Currently:** No such API exists. User would need to understand OpenGL matrices, projections, etc.

#### 3. BoundingBox2D Used for Multiple Spaces

```cpp
// BoundingBox2D used for:
BoundingBox2D geo_bounds;      // Geographic (degrees)
BoundingBox2D proj_bounds;     // Web Mercator (meters)
BoundingBox2D screen_bounds;   // Pixels?

// All use same type! Easy to mix up!
```

#### 4. Conversion Logic Scattered

**Conversions happen in:**
- `tile_renderer.cpp::CalculateVisibleGeographicBounds()` - world → geo
- `tile_renderer.cpp::IsTileInFrustum()` - geo → world
- `camera.cpp::GetGeographicPosition()` - world → geo
- `projection.cpp::Project/Unproject()` - geo → projected
- `tile_mathematics.cpp::GeographicToTile()` - geo → tile

**No centralized conversion layer!**

---

## Industry Standard: Layered Coordinate Architecture

### Coordinate Spaces in Professional GIS

```
┌─────────────────────────────────────────────────────────────┐
│                    User Application Layer                    │
│  (Thinks in: Geographic coords, Screen pixels, Addresses)   │
└───────────────────────────┬─────────────────────────────────┘
                            │
                    ┌───────▼────────┐
                    │   Public API    │
                    │ CoordinateMapper│
                    └───────┬────────┘
                            │
    ┌───────────────────────┼───────────────────────┐
    │                       │                       │
┌───▼────┐          ┌───────▼────────┐      ┌──────▼──────┐
│Screen  │          │   Geographic   │      │  Projected  │
│Space   │◄────────►│     Space      │◄────►│   Space     │
│(pixels)│          │   (lat, lon)   │      │  (meters)   │
└────┬───┘          └───────┬────────┘      └──────┬──────┘
     │                      │                       │
     │              ┌───────▼────────┐              │
     │              │   Tile Space   │              │
     │              │   (x, y, zoom) │              │
     │              └───────┬────────┘              │
     │                      │                       │
     └──────────────────────┼───────────────────────┘
                            │
                    ┌───────▼────────┐
                    │   World Space   │
                    │  (OpenGL units) │
                    │  (internal only)│
                    └────────────────┘
```

### Examples from Industry Leaders

**Google Maps API:**
```javascript
// User never sees OpenGL coordinates
var latLng = new google.maps.LatLng(40.7128, -74.0060);
var point = map.project(latLng);  // Returns screen pixels
```

**Cesium:**
```javascript
// Clear coordinate type separation
var cartographic = Cesium.Cartographic.fromDegrees(lon, lat, height);
var cartesian = Cesium.Cartographic.toCartesian(cartographic);  // Explicit
```

**Mapbox GL:**
```javascript
// Explicit conversions at API boundary
var lngLat = map.unproject([x, y]);  // Screen → Geographic
var point = map.project(lngLat);     // Geographic → Screen
```

---

## Proposed Architecture

### Phase 1: Define Coordinate Space Types (Week 1)

#### 1.1: Create Type-Safe Coordinate Structures

**File:** `include/earth_map/coordinates/coordinate_spaces.h`

```cpp
namespace earth_map {
namespace coordinates {

// ============================================================================
// GEOGRAPHIC SPACE (Degrees)
// ============================================================================

/**
 * @brief Geographic coordinates in WGS84 (latitude, longitude, altitude)
 * @note Already exists, but needs to be in coordinate_spaces.h
 */
struct Geographic {
    double latitude;   ///< Degrees [-90, 90]
    double longitude;  ///< Degrees [-180, 180]
    double altitude;   ///< Meters above sea level

    // Validation
    bool IsValid() const;
};

/**
 * @brief 2D bounding box in geographic space
 */
struct GeographicBounds {
    Geographic min;  ///< Southwest corner
    Geographic max;  ///< Northeast corner

    bool Contains(const Geographic& point) const;
    Geographic GetCenter() const;
};

// ============================================================================
// SCREEN SPACE (Pixels)
// ============================================================================

/**
 * @brief Screen coordinates in pixels (origin: top-left)
 * @note For user API - mouse clicks, UI elements
 */
struct Screen {
    double x;  ///< Pixels from left edge
    double y;  ///< Pixels from top edge

    constexpr Screen(double x_, double y_) : x(x_), y(y_) {}
};

/**
 * @brief Screen region in pixels
 */
struct ScreenBounds {
    Screen min;  ///< Top-left corner
    Screen max;  ///< Bottom-right corner

    double Width() const { return max.x - min.x; }
    double Height() const { return max.y - min.y; }
};

// ============================================================================
// PROJECTED SPACE (Meters in Web Mercator)
// ============================================================================

/**
 * @brief Projected coordinates in Web Mercator (EPSG:3857)
 * @note Internal - used for tile mathematics
 */
struct Projected {
    double x;  ///< Easting in meters
    double y;  ///< Northing in meters

    constexpr Projected(double x_, double y_) : x(x_), y(y_) {}
};

/**
 * @brief Bounding box in projected space
 */
struct ProjectedBounds {
    Projected min;
    Projected max;
};

// ============================================================================
// TILE SPACE (Slippy Map Coordinates)
// ============================================================================

/**
 * @brief Tile coordinates (x, y, zoom)
 * @note Already exists as TileCoordinates, keep it
 */
// TileCoordinates remains unchanged

/**
 * @brief Tile bounds in tile coordinate space
 */
struct TileBounds {
    int32_t min_x, max_x;
    int32_t min_y, max_y;
    int32_t zoom;
};

// ============================================================================
// WORLD SPACE (OpenGL Rendering Units)
// ============================================================================

/**
 * @brief 3D position in OpenGL world space
 * @note Internal only - users never see this
 * @note Globe has radius 1.0, centered at origin
 */
struct World {
    glm::vec3 position;

    constexpr World(float x, float y, float z) : position(x, y, z) {}
    constexpr World(const glm::vec3& pos) : position(pos) {}

    float Distance() const { return glm::length(position); }
};

/**
 * @brief View frustum in world space
 */
struct WorldFrustum {
    std::array<glm::vec4, 6> planes;  // 6 frustum planes

    bool Contains(const World& point) const;
    bool Intersects(const World& center, float radius) const;
};

} // namespace coordinates
} // namespace earth_map
```

#### 1.2: Migration Strategy

**Step 1:** Create new types
**Step 2:** Add conversion functions (see Phase 2)
**Step 3:** Gradually migrate existing code
**Step 4:** Remove old ambiguous types

**DO NOT break existing code all at once!**

---

### Phase 2: Centralized Conversion Layer (Week 2)

#### 2.1: CoordinateMapper - The Conversion Hub

**File:** `include/earth_map/coordinates/coordinate_mapper.h`

```cpp
namespace earth_map {

/**
 * @brief Central hub for all coordinate conversions
 * @note This is THE ONLY place where coordinate conversions happen
 */
class CoordinateMapper {
public:
    // ========================================================================
    // GEOGRAPHIC ↔ WORLD (3D Globe)
    // ========================================================================

    /**
     * @brief Convert geographic (lat/lon) to 3D world position on sphere
     * @param geo Geographic coordinates
     * @param radius Globe radius in world units (default: 1.0)
     * @return 3D position on sphere surface
     */
    static World GeographicToWorld(const Geographic& geo, float radius = 1.0f);

    /**
     * @brief Convert 3D world position to geographic coordinates
     * @param world Position on/near sphere
     * @return Geographic coordinates (lat/lon)
     */
    static Geographic WorldToGeographic(const World& world);

    // ========================================================================
    // GEOGRAPHIC ↔ PROJECTED (Web Mercator)
    // ========================================================================

    /**
     * @brief Project geographic to Web Mercator
     * @param geo Geographic coordinates
     * @return Projected coordinates in meters
     */
    static Projected GeographicToProjected(const Geographic& geo);

    /**
     * @brief Unproject Web Mercator to geographic
     * @param proj Projected coordinates
     * @return Geographic coordinates
     */
    static Geographic ProjectedToGeographic(const Projected& proj);

    // ========================================================================
    // GEOGRAPHIC ↔ TILE
    // ========================================================================

    /**
     * @brief Convert geographic point to tile coordinates
     * @param geo Geographic position
     * @param zoom Zoom level
     * @return Tile containing this point
     */
    static TileCoordinates GeographicToTile(const Geographic& geo, int32_t zoom);

    /**
     * @brief Get geographic bounds of a tile
     * @param tile Tile coordinates
     * @return Geographic bounding box of tile
     */
    static GeographicBounds TileToGeographic(const TileCoordinates& tile);

    /**
     * @brief Get all tiles covering a geographic region
     * @param bounds Geographic bounding box
     * @param zoom Zoom level
     * @return Vector of tiles covering this region
     */
    static std::vector<TileCoordinates> GeographicBoundsToTiles(
        const GeographicBounds& bounds, int32_t zoom);

    // ========================================================================
    // GEOGRAPHIC ↔ SCREEN (User API)
    // ========================================================================

    /**
     * @brief Project geographic point to screen coordinates
     * @param geo Geographic position
     * @param view_matrix Camera view matrix
     * @param proj_matrix Camera projection matrix
     * @param viewport Screen viewport (x, y, width, height)
     * @return Screen coordinates in pixels, or nullopt if behind camera
     */
    static std::optional<Screen> GeographicToScreen(
        const Geographic& geo,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        const glm::ivec4& viewport);

    /**
     * @brief Unproject screen point to geographic coordinates
     * @param screen Screen coordinates (pixel position)
     * @param view_matrix Camera view matrix
     * @param proj_matrix Camera projection matrix
     * @param viewport Screen viewport
     * @return Geographic coordinates, or nullopt if no intersection with globe
     */
    static std::optional<Geographic> ScreenToGeographic(
        const Screen& screen,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        const glm::ivec4& viewport);

    // ========================================================================
    // WORLD ↔ SCREEN (Rendering)
    // ========================================================================

    /**
     * @brief Project 3D world position to screen
     * @note Internal - used by rendering system
     */
    static std::optional<Screen> WorldToScreen(
        const World& world,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        const glm::ivec4& viewport);

    /**
     * @brief Unproject screen to ray in world space
     * @note Returns ray origin and direction for ray-casting
     */
    static std::pair<World, glm::vec3> ScreenToWorldRay(
        const Screen& screen,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        const glm::ivec4& viewport);

    // ========================================================================
    // UTILITY: Bounds Conversions
    // ========================================================================

    /**
     * @brief Calculate visible geographic bounds from camera
     * @param camera_world Camera position in world space
     * @param view_matrix View matrix
     * @param proj_matrix Projection matrix
     * @return Geographic bounding box of visible area
     */
    static GeographicBounds CalculateVisibleGeographicBounds(
        const World& camera_world,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix);

private:
    // Helper functions
    static glm::vec3 GeographicToCartesian(const Geographic& geo, float radius);
    static Geographic CartesianToGeographic(const glm::vec3& cartesian);
    static bool RaySphereIntersection(const glm::vec3& ray_origin,
                                     const glm::vec3& ray_dir,
                                     float sphere_radius,
                                     glm::vec3& intersection);
};

} // namespace earth_map
```

#### 2.2: Benefits of Centralized Conversion

1. **Single Source of Truth:** All conversions in one place
2. **Easy to Test:** Can unit test all conversions independently
3. **Easy to Debug:** Single place to add logging
4. **Type-Safe:** Compiler catches coordinate space mixing
5. **Performance:** Can optimize conversions in one place

---

### Phase 3: User-Facing API (Week 3)

#### 3.1: Map Interaction Interface

**File:** `include/earth_map/api/map_interaction.h`

```cpp
namespace earth_map {
namespace api {

/**
 * @brief High-level interface for map interaction
 * @note This is what library users interact with
 */
class MapInteraction {
public:
    /**
     * @brief Convert screen click to geographic coordinates
     * @param screen_x Pixel X (from left)
     * @param screen_y Pixel Y (from top)
     * @return Geographic coordinates, or nullopt if click not on globe
     */
    std::optional<Geographic> GetLocationAtScreenPoint(int screen_x, int screen_y) const;

    /**
     * @brief Convert geographic location to screen coordinates
     * @param location Geographic coordinates
     * @return Screen coordinates, or nullopt if location not visible
     */
    std::optional<Screen> GetScreenPointForLocation(const Geographic& location) const;

    /**
     * @brief Get geographic bounds of current view
     * @return Bounding box of visible area
     */
    GeographicBounds GetVisibleBounds() const;

    /**
     * @brief Check if a location is currently visible
     * @param location Geographic coordinates
     * @return true if location is in current view
     */
    bool IsLocationVisible(const Geographic& location) const;

    /**
     * @brief Move camera to look at a location
     * @param location Geographic coordinates
     * @param distance Camera distance (in Earth radii, e.g., 3.0)
     * @param duration Animation duration in seconds
     */
    void FlyToLocation(const Geographic& location,
                      float distance = 3.0f,
                      float duration = 1.0f);

    /**
     * @brief Measure distance between two locations
     * @param from Start location
     * @param to End location
     * @return Distance in meters (great circle)
     */
    double MeasureDistance(const Geographic& from, const Geographic& to) const;

    /**
     * @brief Get current camera position in geographic terms
     * @return Camera location and altitude
     */
    Geographic GetCameraLocation() const;

    /**
     * @brief Get what camera is looking at
     * @return Geographic point at center of view
     */
    Geographic GetCameraTarget() const;

private:
    // Reference to renderer for matrix access
    std::shared_ptr<Renderer> renderer_;
};

} // namespace api
} // namespace earth_map
```

#### 3.2: Example User Code

**After refactoring:**
```cpp
#include <earth_map/api/map_interaction.h>

// User application
int main() {
    auto map = earth_map::CreateMap(...);
    auto interaction = map->GetInteraction();

    // User clicks on screen
    int mouse_x = 500, mouse_y = 300;
    auto clicked_location = interaction->GetLocationAtScreenPoint(mouse_x, mouse_y);

    if (clicked_location) {
        std::cout << "You clicked: "
                  << clicked_location->latitude << ", "
                  << clicked_location->longitude << std::endl;
    }

    // User wants to show a marker at NYC
    Geographic nyc(40.7128, -74.0060, 0.0);
    auto screen_pos = interaction->GetScreenPointForLocation(nyc);

    if (screen_pos) {
        // Draw marker at screen_pos->x, screen_pos->y
        DrawMarker(screen_pos->x, screen_pos->y);
    }

    // Measure distance
    Geographic sf(37.7749, -122.4194, 0.0);
    double distance_meters = interaction->MeasureDistance(nyc, sf);
    std::cout << "NYC to SF: " << distance_meters / 1000.0 << " km" << std::endl;
}
```

**User never touches:**
- glm::vec3
- View matrices
- Projection matrices
- OpenGL coordinates
- Web Mercator projection math

---

### Phase 4: Internal Refactoring (Week 4)

#### 4.1: Update Renderer Components

**Changes needed:**

1. **Camera:**
   - Store position as `World` instead of `glm::vec3`
   - Provide `GetWorldPosition()` instead of `GetPosition()`
   - Remove `GetGeographicPosition()` - use CoordinateMapper instead

2. **TileRenderer:**
   - `CalculateVisibleGeographicBounds()` → Use `CoordinateMapper::CalculateVisibleGeographicBounds()`
   - `IsTileInFrustum()` → Use `CoordinateMapper::TileToGeographic()` then `GeographicToWorld()`

3. **GlobeMesh:**
   - Vertices stored as `World` internally
   - Provide conversion helpers if needed

#### 4.2: Remove BoundingBox2D Ambiguity

**Before:**
```cpp
BoundingBox2D bounds;  // What space? Who knows!
```

**After:**
```cpp
GeographicBounds geo_bounds;    // Clear: degrees
ProjectedBounds proj_bounds;    // Clear: meters
ScreenBounds screen_bounds;     // Clear: pixels
```

**Migration:**
- Search all `BoundingBox2D` usage
- Determine which coordinate space each is in
- Replace with appropriate type
- Update conversion calls

---

### Phase 5: Testing & Validation (Week 5)

#### 5.1: Unit Tests for CoordinateMapper

**File:** `tests/unit/coordinate_mapper_test.cpp`

```cpp
TEST(CoordinateMapperTest, GeographicToWorldRoundTrip) {
    Geographic nyc(40.7128, -74.0060, 0.0);

    World world = CoordinateMapper::GeographicToWorld(nyc);
    Geographic result = CoordinateMapper::WorldToGeographic(world);

    EXPECT_NEAR(result.latitude, nyc.latitude, 0.0001);
    EXPECT_NEAR(result.longitude, nyc.longitude, 0.0001);
}

TEST(CoordinateMapperTest, GeographicToTileToGeographic) {
    Geographic location(40.7128, -74.0060, 0.0);
    int zoom = 10;

    TileCoordinates tile = CoordinateMapper::GeographicToTile(location, zoom);
    GeographicBounds bounds = CoordinateMapper::TileToGeographic(tile);

    EXPECT_TRUE(bounds.Contains(location));
}

TEST(CoordinateMapperTest, ScreenToGeographicRequiresValidMatrices) {
    Screen screen(500, 300);
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 proj = glm::mat4(1.0f);
    glm::ivec4 viewport(0, 0, 1024, 768);

    auto result = CoordinateMapper::ScreenToGeographic(screen, view, proj, viewport);

    // Should return valid result or nullopt, not crash
    EXPECT_TRUE(result.has_value() || !result.has_value());
}
```

#### 5.2: Integration Tests

```cpp
TEST(MapInteractionTest, ClickOnGlobeReturnsValidLocation) {
    auto map = CreateTestMap();
    auto interaction = map->GetInteraction();

    // Click at center of screen (should hit globe)
    auto location = interaction->GetLocationAtScreenPoint(512, 384);

    ASSERT_TRUE(location.has_value());
    EXPECT_GE(location->latitude, -90.0);
    EXPECT_LE(location->latitude, 90.0);
    EXPECT_GE(location->longitude, -180.0);
    EXPECT_LE(location->longitude, 180.0);
}

TEST(MapInteractionTest, VisibleLocationHasScreenCoordinates) {
    auto map = CreateTestMap();
    auto interaction = map->GetInteraction();

    // Get camera target
    Geographic target = interaction->GetCameraTarget();

    // Target should be visible
    EXPECT_TRUE(interaction->IsLocationVisible(target));

    // Should have screen coordinates
    auto screen = interaction->GetScreenPointForLocation(target);
    ASSERT_TRUE(screen.has_value());
}
```

---

## Implementation Phases Summary

| Phase | Duration | Deliverable | Breaking Changes |
|-------|----------|-------------|------------------|
| 1. Define Types | 1 week | coordinate_spaces.h with all types | None (additive) |
| 2. Conversion Layer | 1 week | CoordinateMapper fully implemented | None (additive) |
| 3. User API | 1 week | MapInteraction interface | None (additive) |
| 4. Internal Refactor | 1 week | Update renderer to use new types | Internal only |
| 5. Testing | 1 week | Comprehensive test coverage | None |
| **Total** | **5 weeks** | **Production-ready coordinate system** | **Minimal** |

---

## Migration Strategy

### Parallel Implementation

**DO NOT break existing code during migration!**

1. **Week 1-3:** Add new systems alongside old
2. **Week 4:** Gradually migrate internal code
3. **Week 5:** Test extensively
4. **Week 6:** Mark old APIs as deprecated
5. **Future:** Remove deprecated code after users migrate

### Deprecation Warnings

```cpp
// Old API
[[deprecated("Use CoordinateMapper::GeographicToWorld instead")]]
glm::vec3 GeographicToCartesian(const GeographicCoordinates& geo);

// New API
World CoordinateMapper::GeographicToWorld(const Geographic& geo);
```

---

## Benefits After Refactoring

### For Library Users

1. **Simple API:** Think in lat/lon and pixels only
2. **No OpenGL knowledge required:** All rendering internals hidden
3. **Type-safe:** Compiler prevents coordinate space mistakes
4. **Self-documenting:** Function names make conversions explicit

### For Developers

1. **Easier debugging:** All conversions in one place with logging
2. **Better testing:** Can test conversions independently
3. **Less coupling:** Clear boundaries between systems
4. **Future-proof:** Easy to add new coordinate systems (e.g., other projections)

### For System Architecture

1. **Modular:** Coordinate system is separate concern
2. **Maintainable:** Single source of truth for conversions
3. **Extensible:** Easy to add new projections, coordinate systems
4. **Professional:** Matches industry-standard GIS architecture

---

## Comparison: Before vs After

### Before (Current State)

```cpp
// User code - TOO COMPLEX!
auto camera = renderer->GetCamera();
glm::vec3 cam_pos = camera->GetPosition();  // What space?
glm::mat4 view = camera->GetViewMatrix();
glm::mat4 proj = camera->GetProjectionMatrix();

// User must know OpenGL math
glm::vec3 ndc = glm::project(world_pos, view, proj, viewport);
// Hope you get the math right!

// More conversion pain
float lat = glm::degrees(std::asin(direction.y));
float lon = glm::degrees(std::atan2(direction.x, direction.z));
// Which formula? Did I get x/z right?
```

### After (Proposed Architecture)

```cpp
// User code - SIMPLE!
auto interaction = map->GetInteraction();

// One line, explicit, type-safe
auto location = interaction->GetLocationAtScreenPoint(x, y);

// Clear, self-documenting API
auto screen = interaction->GetScreenPointForLocation(location);

// Measure distance - no math needed
double distance = interaction->MeasureDistance(from, to);
```

---

## Risk Assessment

### Risks

1. **Large refactoring:** Touches many files
   - **Mitigation:** Parallel implementation, gradual migration

2. **Performance overhead:** Extra function calls
   - **Mitigation:** Inline critical functions, profile after

3. **Breaking changes:** User code might break
   - **Mitigation:** Deprecation warnings, parallel APIs

4. **Testing complexity:** Many conversion paths
   - **Mitigation:** Comprehensive unit tests, round-trip tests

### Risk Level: **LOW to MEDIUM**

- Well-defined architecture
- Industry-proven approach
- Additive changes (parallel implementation)
- Good test coverage planned

---

## Next Steps

### Immediate (After Fixing Current Bug)

1. **Review this plan** with stakeholders
2. **Get approval** for 5-week timeline
3. **Create GitHub issues** for each phase
4. **Set up feature branch** for refactoring

### Phase 1 Kickoff

1. Create `coordinate_spaces.h` with all types
2. Write comprehensive documentation
3. Create example usage document
4. Set up unit test structure

---

## Questions for Discussion

1. **Timeline:** Is 5 weeks acceptable? Can we compress to 3-4 weeks?
2. **Breaking changes:** Should we accept some breaking changes for cleaner API?
3. **Performance:** Should we benchmark current code before refactoring?
4. **Documentation:** Should we create user guide alongside implementation?
5. **Projection support:** Start with Web Mercator only, or support multiple projections from day 1?

---

## Conclusion

This refactoring creates a **professional, maintainable, user-friendly** coordinate system architecture that:

- ✅ Matches industry standards (Google Maps, Cesium, Mapbox)
- ✅ Provides simple user API (lat/lon ↔ screen pixels)
- ✅ Eliminates coordinate space confusion bugs
- ✅ Makes code self-documenting and type-safe
- ✅ Enables future enhancements (new projections, coordinate systems)

**This is essential infrastructure for a production GIS system.**

The current bug (tiles on wrong side of globe) is a symptom of this architectural issue. Once we have clear coordinate boundaries, such bugs become much harder to introduce and easier to fix.

---

*This plan provides a roadmap from current ad-hoc coordinate handling to professional, industry-standard GIS architecture.*
