# Coordinate System Architecture

**Version:** 1.0
**Date:** 2026-01-26
**Status:** Production

---

## Table of Contents

1. [Overview](#overview)
2. [Coordinate Spaces](#coordinate-spaces)
3. [Transformation Pipeline](#transformation-pipeline)
4. [Detailed Transformations](#detailed-transformations)
5. [Ray-Casting System](#ray-casting-system)
6. [Design Decisions](#design-decisions)
7. [Implementation Details](#implementation-details)
8. [Common Pitfalls](#common-pitfalls)
9. [Testing Strategy](#testing-strategy)
10. [References](#references)

---

## Overview

The Earth Map coordinate system manages transformations between five distinct coordinate spaces:

```
Geographic ←→ World ←→ Screen
    ↕          ↕
Projected   (Camera)
    ↕
  Tile
```

### Core Principle

**All transformations must be bijective (reversible) where applicable**, ensuring that:
- Geographic → Screen → Geographic preserves coordinates (within numeric precision)
- The same visual point on the globe produces the same geographic coordinates regardless of camera position

### Central API

All coordinate transformations are centralized in `CoordinateMapper`, a stateless static class that ensures consistency across the entire application.

**Location:** `include/earth_map/coordinates/coordinate_mapper.h`

---

## Coordinate Spaces

### 1. Geographic Coordinates

**Purpose:** Human-readable representation of positions on Earth

**Definition:**
```cpp
struct Geographic {
    double latitude;   // Degrees, range: [-90, +90]
    double longitude;  // Degrees, range: [-180, +180]
    double altitude;   // Meters above WGS84 ellipsoid
};
```

**Conventions:**
- Latitude: 0° at equator, +90° at North Pole, -90° at South Pole
- Longitude: 0° at Prime Meridian (Greenwich), +180° at Date Line (East), -180° at Date Line (West)
- Altitude: 0 at WGS84 ellipsoid surface, positive upward

**Usage:**
- User input/output
- Location queries
- Geographic calculations (distance, bearing, etc.)

---

### 2. World Coordinates (Cartesian 3D)

**Purpose:** 3D rendering and geometric calculations

**Definition:**
```cpp
struct World {
    glm::vec3 position;  // Meters in ECEF coordinate system
};
```

**Coordinate System (ECEF - Earth-Centered, Earth-Fixed):**
```
    +Y (North Pole)
     ↑
     |
     |
     +-------→ +X (lon=90°E, lat=0°)
    /
   /
  ↓ +Z (lon=0°, lat=0° - Prime Meridian)
```

**Conventions:**
- Origin: Earth's center of mass
- +X axis: Intersection of equator and 90°E meridian
- +Y axis: North Pole
- +Z axis: Intersection of equator and Prime Meridian (0°)
- Unit sphere radius: 1.0 (scales by globe_radius parameter)

**Properties:**
- Right-handed coordinate system
- Rotates with the Earth (fixed to Earth's surface)
- Compatible with OpenGL rendering pipeline

**Usage:**
- 3D globe rendering
- Camera transformations
- Ray-sphere intersection
- View/projection matrix calculations

---

### 3. Projected Coordinates (Web Mercator)

**Purpose:** 2D map tiles and flat map projections

**Definition:**
```cpp
struct Projected {
    double x;  // Meters, range: [-20037508.34, +20037508.34]
    double y;  // Meters, range: [-20037508.34, +20037508.34]
};
```

**Projection:** Web Mercator (EPSG:3857)
- Used by Google Maps, OpenStreetMap, Mapbox, etc.
- Conformal (preserves local angles and shapes)
- Not equal-area (distorts sizes, especially near poles)

**Latitude Limits:**
- Maximum: ±85.05112878° (where y → ±∞)
- Beyond this range: projection returns invalid coordinates

**Usage:**
- Tile coordinate calculations
- 2D map rendering
- Interoperability with web mapping services

---

### 4. Tile Coordinates

**Purpose:** Identifying specific map tiles at different zoom levels

**Definition:**
```cpp
struct TileCoordinates {
    int32_t x;      // Tile column
    int32_t y;      // Tile row
    int32_t zoom;   // Zoom level
};
```

**Tile Numbering:**
```
Zoom 0: 1×1 tiles (entire world)
Zoom 1: 2×2 tiles
Zoom 2: 4×4 tiles
Zoom n: 2^n × 2^n tiles
```

**Coordinate System:**
- Origin (0,0): Top-left corner at northwest (max lat, min lon)
- X increases eastward
- Y increases southward
- At zoom level `z`: x,y ∈ [0, 2^z - 1]

**Usage:**
- Fetching map tiles from tile servers
- Determining visible tiles for rendering
- LOD (Level of Detail) management

---

### 5. Screen Coordinates

**Purpose:** 2D viewport positions for rendering and user interaction

**Definition:**
```cpp
struct Screen {
    double x;  // Pixels from left edge
    double y;  // Pixels from bottom edge (OpenGL convention)
};
```

**Critical Convention:**
```
GLFW (Input):           OpenGL (Rendering):
Y=0 ───────────  Top    Y=height ──────  Top
  │                       │
  │                       │
  │                       │
Y=height ───── Bottom   Y=0 ─────────── Bottom
```

**Conversion (GLFW → OpenGL):**
```cpp
opengl_y = window_height - glfw_y;
```

**After Y-flip:**
- Y = 0: Bottom of viewport
- Y = viewport_height: Top of viewport
- Matches OpenGL NDC convention

**HiDPI/Retina Displays:**
- Window size ≠ Framebuffer size
- Must use `glGetIntegerv(GL_VIEWPORT, ...)` for actual dimensions
- Must scale mouse coordinates: `mouse * (viewport_size / window_size)`

**Usage:**
- Mouse click handling
- Screen-to-world ray casting
- UI overlay positioning

---

## Transformation Pipeline

### Forward Pipeline (Geographic → Screen)

```
Geographic (lat, lon, alt)
    │
    │ GeographicToWorld()
    │ [Spherical → Cartesian conversion]
    │
    ↓
World (x, y, z)
    │
    │ WorldToScreen()
    │ [View transform + Projection transform + Viewport transform]
    │
    ↓
Screen (x, y)
```

**Use Case:** Project a location on Earth to a pixel position on screen

**Example:**
```cpp
Geographic nyc(40.7128, -74.0060, 0.0);
World world = CoordinateMapper::GeographicToWorld(nyc, EARTH_RADIUS);
auto screen = CoordinateMapper::WorldToScreen(world, view, proj, viewport);
// screen->x, screen->y = pixel position of NYC on screen
```

---

### Reverse Pipeline (Screen → Geographic)

```
Screen (x, y)
    │
    │ ScreenToWorldRay()
    │ [Inverse viewport + Inverse projection + Inverse view]
    │
    ↓
Ray (origin, direction)
    │
    │ RaySphereIntersection()
    │ [Solve quadratic equation]
    │
    ↓
World (x, y, z)
    │
    │ WorldToGeographic()
    │ [Cartesian → Spherical conversion]
    │
    ↓
Geographic (lat, lon, alt)
```

**Use Case:** Convert mouse click to geographic coordinates

**Example:**
```cpp
Screen click(mouse_x, window_height - mouse_y);  // Y-flip
auto geo = CoordinateMapper::ScreenToGeographic(click, view, proj, viewport, EARTH_RADIUS);
// geo->latitude, geo->longitude = location clicked on globe
```

---

### Tile System Pipeline

```
Geographic (lat, lon)
    │
    │ GeographicToProjected()
    │ [Web Mercator projection]
    │
    ↓
Projected (x, y)
    │
    │ TileMathematics::GeographicToTile()
    │ [Normalize to [0,1] → Scale by 2^zoom → Floor to tile indices]
    │
    ↓
TileCoordinates (x, y, zoom)
```

**Reverse:**
```
TileCoordinates (x, y, zoom)
    │
    │ TileMathematics::GetTileBounds()
    │ [Calculate tile corners in geographic space]
    │
    ↓
GeographicBounds (min, max)
```

---

## Detailed Transformations

### 1. Geographic ↔ World (Spherical ↔ Cartesian)

#### Geographic → World (Forward)

**Formula:**
```cpp
x = radius * cos(lat_rad) * sin(lon_rad)
y = radius * sin(lat_rad)
z = radius * cos(lat_rad) * cos(lon_rad)
```

**Derivation:**
- Standard spherical-to-Cartesian conversion
- `lat_rad = radians(latitude)`
- `lon_rad = radians(longitude)`
- Altitude is added as radial offset: `final_radius = radius + altitude`

**Example:**
```cpp
// Equator, Prime Meridian (0°, 0°)
Geographic(0, 0, 0) → World(0, 0, 1)  // On +Z axis

// North Pole (90°, 0°)
Geographic(90, 0, 0) → World(0, 1, 0)  // On +Y axis

// Equator, 90° East
Geographic(0, 90, 0) → World(1, 0, 0)  // On +X axis
```

**Implementation:**
```cpp
glm::vec3 CoordinateMapper::GeographicToCartesian(const Geographic& geo, float radius) {
    double lat_rad = glm::radians(geo.latitude);
    double lon_rad = glm::radians(geo.longitude);

    float x = radius * std::cos(lat_rad) * std::sin(lon_rad);
    float y = radius * std::sin(lat_rad);
    float z = radius * std::cos(lat_rad) * std::cos(lon_rad);

    return glm::vec3(x, y, z);
}
```

---

#### World → Geographic (Reverse)

**Formula:**
```cpp
direction = normalize(position)
latitude = degrees(asin(direction.y))
longitude = degrees(atan2(direction.x, direction.z))
altitude = length(position) - radius
```

**Special Cases:**
- Poles: `latitude = ±90°`, `longitude` is undefined (set to 0)
- Dateline: `longitude` wraps at ±180°

**Implementation:**
```cpp
Geographic CoordinateMapper::CartesianToGeographic(const glm::vec3& cartesian) {
    glm::vec3 dir = glm::normalize(cartesian);

    double lat = glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
    double lon = glm::degrees(std::atan2(dir.x, dir.z));

    return Geographic(lat, lon, 0.0);
}
```

---

### 2. Geographic ↔ Projected (Web Mercator)

#### Geographic → Projected (Forward)

**Formula:**
```cpp
EARTH_CIRCUMFERENCE = 2 * π * EARTH_RADIUS = 40075016.686 meters
HALF_CIRCUMFERENCE = 20037508.343 meters

x = EARTH_RADIUS * radians(longitude)
y = EARTH_RADIUS * ln(tan(π/4 + radians(latitude)/2))
```

**Normalization:**
```cpp
// Normalize to [-HALF_CIRCUMFERENCE, +HALF_CIRCUMFERENCE]
x_meters = x
y_meters = y
```

**Latitude Limits:**
- Valid range: [-85.05112878°, +85.05112878°]
- Beyond this: `ln(tan(...))` approaches ±∞
- Invalid latitudes return `NaN` (checked with `IsValid()`)

**Implementation:** Uses `ProjectionRegistry::GetProjection(WEB_MERCATOR)`

---

#### Projected → Geographic (Reverse)

**Formula:**
```cpp
longitude = degrees(x / EARTH_RADIUS)
latitude = degrees(2 * atan(exp(y / EARTH_RADIUS)) - π/2)
```

**Implementation:** Uses `ProjectionRegistry::GetProjection(WEB_MERCATOR)->Unproject()`

---

### 3. Geographic ↔ Tile

#### Geographic → Tile (Forward)

**Algorithm:**
```cpp
// Step 1: Project to Web Mercator
projected = GeographicToProjected(geo)

// Step 2: Normalize to [0, 1]
norm_x = (projected.x + HALF_CIRCUMFERENCE) / (2 * HALF_CIRCUMFERENCE)
norm_y = (projected.y + HALF_CIRCUMFERENCE) / (2 * HALF_CIRCUMFERENCE)

// Step 3: Scale by tile count at zoom level
num_tiles = 2^zoom
tile_x = floor(norm_x * num_tiles)
tile_y = floor(norm_y * num_tiles)

// Step 4: Clamp to valid range
tile_x = clamp(tile_x, 0, num_tiles - 1)
tile_y = clamp(tile_y, 0, num_tiles - 1)
```

**Y-Axis Orientation:**
- Web Mercator Y: Positive northward
- Tile Y: Positive southward (row 0 at top)
- Requires Y-flip: `tile_y = num_tiles - 1 - tile_y`

**Implementation:** `TileMathematics::GeographicToTile()`

---

#### Tile → Geographic (Reverse)

**Algorithm:**
```cpp
// Each tile covers a specific geographic bounds
num_tiles = 2^zoom

// Tile covers normalized range
norm_x_min = tile_x / num_tiles
norm_x_max = (tile_x + 1) / num_tiles
norm_y_min = tile_y / num_tiles
norm_y_max = (tile_y + 1) / num_tiles

// Convert to Web Mercator meters
x_min = norm_x_min * (2 * HALF_CIRCUMFERENCE) - HALF_CIRCUMFERENCE
x_max = norm_x_max * (2 * HALF_CIRCUMFERENCE) - HALF_CIRCUMFERENCE
y_min = norm_y_min * (2 * HALF_CIRCUMFERENCE) - HALF_CIRCUMFERENCE
y_max = norm_y_max * (2 * HALF_CIRCUMFERENCE) - HALF_CIRCUMFERENCE

// Unproject to geographic
geo_min = ProjectedToGeographic(Projected(x_min, y_min))
geo_max = ProjectedToGeographic(Projected(x_max, y_max))
```

**Returns:** `GeographicBounds(geo_min, geo_max)`

**Implementation:** `TileMathematics::GetTileBounds()`

---

### 4. World ↔ Screen (3D Rendering Pipeline)

#### World → Screen (Forward)

**Pipeline:**
```
World Space
    │
    │ × View Matrix (camera transform)
    ↓
View Space (Camera Space)
    │
    │ × Projection Matrix (perspective)
    ↓
Clip Space
    │
    │ ÷ w (perspective divide)
    ↓
NDC (Normalized Device Coordinates)
    │
    │ Viewport Transform
    ↓
Screen Space
```

**Step-by-Step:**

1. **World → Clip Space:**
```cpp
clip = projection_matrix * view_matrix * vec4(world.position, 1.0)
```

2. **Clip Space Check:**
```cpp
if (clip.w <= 0.0) return nullopt;  // Behind camera
```

3. **Perspective Divide:**
```cpp
ndc.x = clip.x / clip.w
ndc.y = clip.y / clip.w
ndc.z = clip.z / clip.w
```

4. **NDC Bounds Check:**
```cpp
if (|ndc.x| > 1.0 || |ndc.y| > 1.0 || |ndc.z| > 1.0)
    return nullopt;  // Outside view frustum
```

5. **Viewport Transform:**
```cpp
// NDC: [-1, +1] → Screen: [0, viewport_size]
screen.x = (ndc.x * 0.5 + 0.5) * viewport_width + viewport_x
screen.y = (ndc.y * 0.5 + 0.5) * viewport_height + viewport_y
```

**Critical Y-Axis Convention:**
- NDC: Y = -1 (bottom), Y = +1 (top)
- Screen: Y = 0 (bottom), Y = viewport_height (top)
- Both use same upward direction (no flip)

**Implementation:**
```cpp
std::optional<Screen> CoordinateMapper::WorldToScreen(
    const World& world,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix,
    const glm::ivec4& viewport) noexcept
{
    // Transform to clip space
    glm::vec4 clip = proj_matrix * view_matrix * glm::vec4(world.position, 1.0f);

    if (clip.w <= 0.0f) return std::nullopt;

    // Perspective divide to NDC
    glm::vec3 ndc = glm::vec3(clip) / clip.w;

    if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f) return std::nullopt;

    // Viewport transform
    double screen_x = (ndc.x * 0.5 + 0.5) * viewport[2] + viewport[0];
    double screen_y = (ndc.y * 0.5 + 0.5) * viewport[3] + viewport[1];

    return Screen(screen_x, screen_y);
}
```

---

#### Screen → World (Reverse - Ray Casting)

**Problem:** Screen → World is not unique (infinite points along ray)

**Solution:** Generate a ray in world space, intersect with globe sphere

**Pipeline:**
```
Screen Space
    │
    │ Inverse Viewport Transform
    ↓
NDC (Normalized Device Coordinates)
    │
    │ Inverse Projection (unproject near/far planes)
    ↓
View Space (Camera Space)
    │
    │ Inverse View Transform
    ↓
World Space Ray (origin + direction)
```

**Step-by-Step:**

1. **Screen → NDC:**
```cpp
// CRITICAL: Screen Y is 0 at bottom, viewport[3] at top
// NDC Y is -1 at bottom, +1 at top
ndc.x = 2.0 * (screen.x - viewport[0]) / viewport[2] - 1.0
ndc.y = 2.0 * (screen.y - viewport[1]) / viewport[3] - 1.0
```

**Historical Bug (FIXED):**
```cpp
// OLD (WRONG - inverted Y):
ndc.y = 1.0 - 2.0 * (screen.y - viewport[1]) / viewport[3]

// This caused rays to point in wrong vertical direction!
// Bug was invisible at screen center (ndc.y = 0) but failed elsewhere
```

2. **Unproject Near Plane:**
```cpp
near_clip = vec4(ndc.x, ndc.y, -1.0, 1.0)  // NDC z=-1 is near plane
near_view = inverse(proj_matrix) * near_clip
near_view /= near_view.w  // Perspective divide
near_world = inverse(view_matrix) * near_view
near_world /= near_world.w
```

3. **Unproject Far Plane:**
```cpp
far_clip = vec4(ndc.x, ndc.y, +1.0, 1.0)  // NDC z=+1 is far plane
far_view = inverse(proj_matrix) * far_clip
far_view /= far_view.w
far_world = inverse(view_matrix) * far_view
far_world /= far_world.w
```

4. **Compute Ray:**
```cpp
ray_origin = camera_position  // From inverse view matrix
ray_direction = normalize(far_world - near_world)
```

**Implementation:**
```cpp
std::pair<World, glm::vec3> CoordinateMapper::ScreenToWorldRay(
    const Screen& screen,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix,
    const glm::ivec4& viewport) noexcept
{
    glm::mat4 inv_proj = glm::inverse(proj_matrix);
    glm::mat4 inv_view = glm::inverse(view_matrix);

    glm::vec3 camera_pos = glm::vec3(inv_view[3]);

    // Screen → NDC (CORRECTED)
    float ndc_x = (2.0f * (screen.x - viewport[0])) / viewport[2] - 1.0f;
    float ndc_y = (2.0f * (screen.y - viewport[1])) / viewport[3] - 1.0f;

    // Unproject near and far planes
    glm::vec4 near_clip(ndc_x, ndc_y, -1.0f, 1.0f);
    glm::vec4 near_view = inv_proj * near_clip;
    near_view /= near_view.w;
    glm::vec4 near_world_4 = inv_view * near_view;
    glm::vec3 near_world = glm::vec3(near_world_4) / near_world_4.w;

    glm::vec4 far_clip(ndc_x, ndc_y, 1.0f, 1.0f);
    glm::vec4 far_view = inv_proj * far_clip;
    far_view /= far_view.w;
    glm::vec4 far_world_4 = inv_view * far_view;
    glm::vec3 far_world = glm::vec3(far_world_4) / far_world_4.w;

    glm::vec3 ray_dir = glm::normalize(far_world - near_world);

    return {World(camera_pos), ray_dir};
}
```

---

## Ray-Casting System

### Overview

Ray-casting converts 2D screen clicks into 3D geographic coordinates by:
1. Generating a ray from camera through the clicked pixel
2. Intersecting the ray with the globe sphere
3. Converting the intersection point to geographic coordinates

### Ray-Sphere Intersection

**Sphere Equation:**
```
|P - C|² = r²
where:
  P = point on sphere
  C = sphere center (0, 0, 0)
  r = sphere radius
```

**Ray Equation:**
```
P(t) = O + t·D
where:
  O = ray origin (camera position)
  D = ray direction (normalized)
  t = distance along ray (t ≥ 0)
```

**Substituting Ray into Sphere:**
```
|O + t·D - C|² = r²

Let oc = O - C:
|oc + t·D|² = r²

Expanding:
(oc + t·D)·(oc + t·D) = r²
oc·oc + 2t(oc·D) + t²(D·D) = r²

Rearranging to quadratic form:
at² + bt + c = 0

where:
  a = D·D = 1 (since D is normalized)
  b = 2(oc·D)
  c = oc·oc - r²
```

**Solving Quadratic:**
```
discriminant = b² - 4ac

if discriminant < 0:
    No intersection (ray misses sphere)

t₁ = (-b - √discriminant) / (2a)  // Near intersection
t₂ = (-b + √discriminant) / (2a)  // Far intersection
```

**Choosing Intersection:**

```cpp
if (t₁ > 0 && t₂ > 0):
    Use t₁ (front face - camera outside sphere)
elif (t₁ > 0):
    Use t₁ (only near hit is valid)
elif (t₂ > 0):
    Use t₂ (camera inside sphere - use far hit)
else:
    No valid intersection (sphere behind camera)
```

**Computing Hit Point:**
```cpp
hit_point = ray_origin + t * ray_direction
```

### Implementation

```cpp
bool CoordinateMapper::RaySphereIntersection(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_dir,
    const glm::vec3& sphere_center,
    float sphere_radius,
    glm::vec3& intersection_point) noexcept
{
    glm::vec3 oc = ray_origin - sphere_center;

    float a = glm::dot(ray_dir, ray_dir);
    float b = 2.0f * glm::dot(oc, ray_dir);
    float c = glm::dot(oc, oc) - sphere_radius * sphere_radius;

    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f) return false;  // No intersection

    float sqrt_disc = std::sqrt(discriminant);
    float t1 = (-b - sqrt_disc) / (2.0f * a);
    float t2 = (-b + sqrt_disc) / (2.0f * a);

    float t;
    if (t1 > 0.0f && t2 > 0.0f) {
        t = t1;  // Use front face
    } else if (t1 > 0.0f) {
        t = t1;
    } else if (t2 > 0.0f) {
        t = t2;
    } else {
        return false;  // Both behind camera
    }

    intersection_point = ray_origin + t * ray_dir;
    return true;
}
```

### Complete Screen → Geographic Pipeline

```cpp
std::optional<Geographic> CoordinateMapper::ScreenToGeographic(
    const Screen& screen,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix,
    const glm::ivec4& viewport,
    float globe_radius) noexcept
{
    // Step 1: Generate ray
    auto [ray_origin, ray_dir] = ScreenToWorldRay(screen, view, proj, viewport);

    // Step 2: Intersect with sphere
    glm::vec3 intersection;
    bool hit = RaySphereIntersection(
        ray_origin.position,
        ray_dir,
        glm::vec3(0.0f),  // Sphere at origin
        globe_radius,
        intersection
    );

    if (!hit) return std::nullopt;

    // Step 3: Convert to geographic
    return CartesianToGeographic(intersection);
}
```

---

## Design Decisions

### 1. Centralized API (CoordinateMapper)

**Decision:** All coordinate transformations through a single static class

**Rationale:**
- **Consistency:** Single source of truth for conversions
- **Maintainability:** Changes in one place affect entire system
- **Testability:** Easy to test all transformations in isolation
- **Performance:** Stateless design allows compiler optimizations

**Alternatives Rejected:**
- Instance-based mapper (unnecessary state overhead)
- Free functions (harder to discover, no namespace organization)
- Per-space conversion classes (fragmented API)

---

### 2. Y-Axis Convention (OpenGL Standard)

**Decision:** Screen Y = 0 at bottom, positive upward (post-flip)

**Rationale:**
- **Consistency with OpenGL:** NDC and Screen use same convention
- **Mathematical correctness:** No hidden inversions in pipeline
- **Bug prevention:** Symmetric conversions (forward/reverse use same formula)

**Trade-off:**
- Requires Y-flip at GLFW input boundary
- Extra cognitive load for developers familiar with window Y-down convention

**Implementation:**
```cpp
// At GLFW boundary (basic_example.cpp):
Screen screen_point(mouse_x, window_height - mouse_y);  // One-time flip
```

---

### 3. Optional Return Types

**Decision:** Use `std::optional<T>` for fallible conversions

**Rationale:**
- **Type Safety:** Compiler enforces null checks
- **Self-Documenting:** API clearly indicates possible failure
- **No Exceptions:** Performance-critical code paths

**Example:**
```cpp
auto geo = ScreenToGeographic(screen, ...);
if (geo.has_value()) {
    // Use geo->latitude, geo->longitude
} else {
    // Handle miss (ray didn't hit globe)
}
```

---

### 4. Stateless Design

**Decision:** CoordinateMapper has no member variables, all static methods

**Rationale:**
- **Thread Safety:** No shared state, fully thread-safe
- **Simplicity:** No initialization, no lifetime management
- **Performance:** No vtable lookups, inline-friendly

**Example:**
```cpp
// No instance needed
auto world = CoordinateMapper::GeographicToWorld(geo);
```

---

### 5. Normalized Coordinates for Tiles

**Decision:** Tiles use power-of-2 subdivision (2^zoom × 2^zoom grid)

**Rationale:**
- **Web Compatibility:** Matches industry standard (Google Maps, OSM)
- **Efficient Computation:** Bit-shift operations instead of division
- **Hierarchical LOD:** Natural parent-child tile relationships

**Formula:**
```cpp
// Parent tile at zoom n-1 contains 4 child tiles at zoom n
parent_x = child_x / 2;
parent_y = child_y / 2;
```

---

### 6. Explicit Viewport Parameter

**Decision:** All screen transformations require explicit `glm::ivec4 viewport`

**Rationale:**
- **HiDPI Support:** Window size ≠ framebuffer size on retina displays
- **Multi-Viewport:** Supports rendering to different viewports (main, minimap)
- **Correctness:** Forces developer to use actual OpenGL viewport

**Best Practice:**
```cpp
GLint gl_viewport[4];
glGetIntegerv(GL_VIEWPORT, gl_viewport);
glm::ivec4 viewport(gl_viewport[0], gl_viewport[1], gl_viewport[2], gl_viewport[3]);
```

**Anti-Pattern:**
```cpp
// WRONG: Don't use window size on HiDPI displays
int width, height;
glfwGetWindowSize(window, &width, &height);
glm::ivec4 viewport(0, 0, width, height);  // BUG on retina!
```

---

### 7. Unit Sphere with Configurable Radius

**Decision:** Internal calculations use unit sphere (radius=1.0), scale as needed

**Rationale:**
- **Numerical Stability:** Avoids large numbers (6.3M meters) in calculations
- **Flexibility:** Easy to render at any scale
- **Compatibility:** Math libraries expect normalized values

**Usage:**
```cpp
// Render at actual Earth size
const float EARTH_RADIUS = 6371000.0f;  // meters
World world = GeographicToWorld(geo, EARTH_RADIUS);

// Or use normalized scale for testing
World unit = GeographicToWorld(geo, 1.0f);
```

---

## Implementation Details

### File Structure

```
include/earth_map/coordinates/
├── coordinate_spaces.h        # Type definitions (Geographic, World, etc.)
├── coordinate_mapper.h        # Central transformation API
├── altitude_reference.h       # Altitude datum handling
└── coordinate_compat.h        # Legacy compatibility layer

src/coordinates/
├── coordinate_mapper.cpp      # Core transformation implementations
└── altitude_reference.cpp     # Altitude conversions

include/earth_map/math/
├── projection.h               # Projection interface (Web Mercator, etc.)
└── tile_mathematics.h         # Tile coordinate calculations

src/math/
├── projection.cpp             # Projection implementations
└── tile_mathematics.cpp       # Tile math implementations
```

### Key Classes

#### CoordinateMapper

**Location:** `include/earth_map/coordinates/coordinate_mapper.h`

**Purpose:** Central API for all coordinate transformations

**Methods:**
```cpp
class CoordinateMapper {
public:
    // Geographic ↔ World
    static World GeographicToWorld(const Geographic& geo, float radius = 1.0f);
    static Geographic WorldToGeographic(const World& world, float radius = 1.0f);

    // Geographic ↔ Projected
    static Projected GeographicToProjected(const Geographic& geo);
    static Geographic ProjectedToGeographic(const Projected& proj);

    // Geographic ↔ Tile
    static TileCoordinates GeographicToTile(const Geographic& geo, int32_t zoom);
    static GeographicBounds TileToGeographic(const TileCoordinates& tile);
    static std::vector<TileCoordinates> GeographicBoundsToTiles(
        const GeographicBounds& bounds, int32_t zoom);

    // Geographic ↔ Screen
    static std::optional<Screen> GeographicToScreen(
        const Geographic& geo,
        const glm::mat4& view, const glm::mat4& proj, const glm::ivec4& viewport);
    static std::optional<Geographic> ScreenToGeographic(
        const Screen& screen,
        const glm::mat4& view, const glm::mat4& proj, const glm::ivec4& viewport,
        float globe_radius);

    // World ↔ Screen
    static std::optional<Screen> WorldToScreen(
        const World& world,
        const glm::mat4& view, const glm::mat4& proj, const glm::ivec4& viewport);
    static std::pair<World, glm::vec3> ScreenToWorldRay(
        const Screen& screen,
        const glm::mat4& view, const glm::mat4& proj, const glm::ivec4& viewport);

    // Utility
    static GeographicBounds CalculateVisibleGeographicBounds(
        const World& camera, const glm::mat4& view, const glm::mat4& proj, float radius);

private:
    // Internal helpers
    static glm::vec3 GeographicToCartesian(const Geographic& geo, float radius);
    static Geographic CartesianToGeographic(const glm::vec3& cartesian);
    static bool RaySphereIntersection(
        const glm::vec3& ray_origin, const glm::vec3& ray_dir,
        const glm::vec3& sphere_center, float sphere_radius,
        glm::vec3& intersection_point);
};
```

---

#### ProjectionRegistry

**Location:** `include/earth_map/math/projection.h`

**Purpose:** Manages different map projections (Web Mercator, future: Equirectangular, etc.)

**Interface:**
```cpp
class IProjection {
public:
    virtual Projected Project(const Geographic& geo) const = 0;
    virtual Geographic Unproject(const Projected& proj) const = 0;
    virtual bool IsInBounds(const Geographic& geo) const = 0;
};

class ProjectionRegistry {
public:
    static std::shared_ptr<IProjection> GetProjection(ProjectionType type);
};
```

**Current Implementation:** Web Mercator (EPSG:3857)

---

#### TileMathematics

**Location:** `include/earth_map/math/tile_mathematics.h`

**Purpose:** Tile coordinate calculations and bounds computations

**Methods:**
```cpp
class TileMathematics {
public:
    static TileCoordinates GeographicToTile(const Geographic& geo, int32_t zoom);
    static BoundingBox2D GetTileBounds(const TileCoordinates& tile);
    static std::vector<TileCoordinates> GetTilesInBounds(
        const BoundingBox2D& bounds, int32_t zoom);
};
```

---

### Performance Considerations

#### Matrix Inversion

**Cost:** `O(n³)` for n×n matrix (n=4 for 4×4 matrices)

**Optimization:**
```cpp
// Cache inverse matrices when same matrices used repeatedly
glm::mat4 inv_view = glm::inverse(view_matrix);
glm::mat4 inv_proj = glm::inverse(proj_matrix);

// Reuse for multiple screen points
for (const auto& screen_point : clicks) {
    auto ray = ScreenToWorldRay(screen_point, inv_view, inv_proj, viewport);
    // ...
}
```

#### Trigonometric Functions

**Cost:** `sin`, `cos`, `tan`, `atan2` are expensive (10-100 cycles)

**Mitigation:**
- Use lookup tables for repeated angles (not currently implemented)
- Batch conversions where possible
- Profile before optimizing (premature optimization is root of evil)

#### Floating-Point Precision

**Consideration:** Geographic coordinates use `double` (64-bit) for precision

**Rationale:**
- Earth circumference ≈ 40M meters
- `float` (32-bit) has ~7 decimal digits → ~5m precision at worst case
- `double` (64-bit) has ~15 decimal digits → millimeter precision globally

**Trade-off:**
- World/Screen coordinates use `float` (sufficient for rendering)
- Geographic/Projected use `double` (necessary for accuracy)

---

## Common Pitfalls

### 1. Y-Axis Inversion Bug

**Problem:** Inverting Y-coordinate during Screen ↔ NDC conversion

**Symptom:** Clicking the same visual point from different camera angles gives different coordinates

**Root Cause:**
```cpp
// WRONG (inverts Y):
float ndc_y = 1.0f - (2.0f * screen.y) / viewport_height;

// CORRECT:
float ndc_y = (2.0f * screen.y) / viewport_height - 1.0f;
```

**Detection:** Bug is invisible at screen center (NDC 0,0), only manifests off-center

**Prevention:** Always test off-center screen points in unit tests

---

### 2. Viewport Size on HiDPI Displays

**Problem:** Using window size instead of actual OpenGL viewport

**Symptom:** Clicks are off by a constant factor (usually 2× on retina displays)

**Root Cause:**
```cpp
// WRONG on HiDPI:
int width, height;
glfwGetWindowSize(window, &width, &height);  // Returns logical size
glm::ivec4 viewport(0, 0, width, height);    // Not framebuffer size!

// CORRECT:
GLint gl_viewport[4];
glGetIntegerv(GL_VIEWPORT, gl_viewport);  // Actual framebuffer size
glm::ivec4 viewport(gl_viewport[0], gl_viewport[1], gl_viewport[2], gl_viewport[3]);
```

**Prevention:** Always use `glGetIntegerv(GL_VIEWPORT)` for coordinate conversions

---

### 3. GLFW Y-Flip Forgotten

**Problem:** Forgetting to flip Y-coordinate at input boundary

**Symptom:** Clicks appear vertically inverted

**Root Cause:**
```cpp
// WRONG (uses raw GLFW Y):
Screen screen(mouse_x, mouse_y);  // Y=0 at top (GLFW convention)

// CORRECT (flip to OpenGL):
Screen screen(mouse_x, window_height - mouse_y);  // Y=0 at bottom
```

**Prevention:** Perform Y-flip immediately at input boundary, never internally

---

### 4. Latitude Out of Web Mercator Bounds

**Problem:** Projecting latitude > 85.05° causes overflow

**Symptom:** `NaN` or infinite projected coordinates

**Root Cause:** Web Mercator `y = ln(tan(π/4 + lat/2))` → ±∞ at poles

**Solution:**
```cpp
auto projected = GeographicToProjected(geo);
if (!projected.IsValid()) {
    // Handle out-of-bounds (use alternative projection or clamp)
}
```

**Prevention:** Always check `IsValid()` after projection

---

### 5. Assuming Unique Screen → World Mapping

**Problem:** Treating screen point as mapping to a single world point

**Reality:** Screen point defines a ray (infinite world points)

**Solution:** Use ray-sphere intersection to find specific surface point

**Example:**
```cpp
// WRONG (no unique point):
World world = ScreenToWorld(screen, ...);  // Doesn't exist!

// CORRECT (ray + intersection):
auto [ray_origin, ray_dir] = ScreenToWorldRay(screen, ...);
glm::vec3 hit;
if (RaySphereIntersection(ray_origin, ray_dir, center, radius, hit)) {
    World world(hit);
}
```

---

### 6. Ignoring Hemisphere Ambiguity

**Problem:** `atan2` returns longitude in [-180°, +180°], but dateline is discontinuous

**Example:**
```cpp
// Same meridian, different representations:
lon = 179.9°  // Just west of dateline
lon = -180.0° // Dateline itself
lon = -179.9° // Just east of dateline
```

**Solution:** Use modulo arithmetic or hemisphere-aware comparisons

**Current Handling:** System normalizes to [-180°, +180°] range

---

### 7. Ray Intersection Choosing Wrong Hit

**Problem:** Using far intersection when camera is outside sphere

**Symptom:** Click registers on back side of globe

**Root Cause:**
```cpp
// WRONG (always uses t2):
float t = t2;  // Far hit - wrong for front-facing globe

// CORRECT:
if (t1 > 0.0f && t2 > 0.0f) {
    float t = t1;  // Use near hit (front face)
}
```

**Prevention:** Always prefer nearer positive intersection

---

## Testing Strategy

### Unit Test Coverage

**File:** `tests/unit/test_coordinate_mapper.cpp`

**Test Suites:**

1. **GeographicWorldTest** - Geographic ↔ World conversions
   - Pole/equator/meridian mappings
   - Round-trip accuracy
   - Custom radius scaling

2. **GeographicProjectedTest** - Geographic ↔ Projected conversions
   - Web Mercator bounds
   - Origin/boundary mappings
   - Out-of-bounds handling

3. **GeographicTileTest** - Geographic ↔ Tile conversions
   - Zoom level calculations
   - Tile bounds containment
   - Multi-tile regions

4. **GeographicScreenTest** - Geographic ↔ Screen conversions
   - Projection to screen
   - Ray-sphere intersection
   - Round-trip consistency

5. **RayCastingTest** - Ray-casting regression tests
   - **Off-center accuracy** (detects Y-flip bug)
   - **All-quadrants consistency** (tests 4 screen corners)
   - **Same visual point from different cameras** (core regression test)
   - **Multiple targets** (various globe locations)
   - **Round-trip accuracy** (Geographic → Screen → Geographic)

### Critical Tests

#### 1. Off-Center Y-Flip Detection

**Purpose:** Detect Y-coordinate inversion bugs (invisible at screen center)

**Implementation:**
```cpp
TEST_F(RayCastingTest, ScreenToGeographic_OffCenter_AccurateResults) {
    // Position camera so target appears OFF-CENTER
    Screen screen_off_center(600.0, 450.0);  // Upper-right of center

    auto geo = ScreenToGeographic(screen_off_center, ...);
    if (!geo.has_value()) return;  // May miss sphere

    // Round-trip: should preserve screen coordinates
    World world = GeographicToWorld(*geo);
    auto screen_back = WorldToScreen(world, ...);

    EXPECT_NEAR(screen_off_center.y, screen_back->y, 1.0)
        << "Y round-trip failed - Y-flip bug detected!";
}
```

**Why It Works:**
- Screen center (400, 300) → NDC (0, 0) is invariant under Y-flip
- Off-center points (600, 450) → NDC (0.5, 0.5) are NOT invariant
- Y-flip bug manifests as large error in Y round-trip

---

#### 2. All-Quadrants Consistency

**Purpose:** Verify coordinate conversions work in all screen regions

**Implementation:**
```cpp
TEST_F(RayCastingTest, ScreenToGeographic_AllQuadrants_Consistent) {
    std::vector<glm::vec2> test_points = {
        {100, 100},   // Bottom-left
        {700, 100},   // Bottom-right
        {100, 500},   // Top-left
        {700, 500}    // Top-right
    };

    for (const auto& pt : test_points) {
        auto geo = ScreenToGeographic(Screen(pt.x, pt.y), ...);
        if (!geo.has_value()) continue;

        auto screen_back = WorldToScreen(GeographicToWorld(*geo), ...);

        EXPECT_NEAR(pt.x, screen_back->x, 1.0);
        EXPECT_NEAR(pt.y, screen_back->y, 1.0);
    }
}
```

---

#### 3. Camera-Independent Consistency

**Purpose:** Same visual point → same coordinates regardless of camera position

**Implementation:**
```cpp
TEST_F(RayCastingTest, ScreenToGeographic_SameVisualPoint_DifferentCameras) {
    Geographic target(0.0, 0.0, 0.0);
    World target_world = GeographicToWorld(target);

    std::vector<glm::vec3> camera_positions = {
        {0, 0, 2.0f},    // Close
        {0, 0, 5.0f},    // Far
        {2, 1, 2.0f}     // Angled
    };

    for (const auto& cam_pos : camera_positions) {
        glm::mat4 view = glm::lookAt(cam_pos, target_world.position, ...);

        auto screen = WorldToScreen(target_world, view, ...);
        auto geo_back = ScreenToGeographic(*screen, view, ...);

        // CRITICAL: Should match original target
        EXPECT_NEAR(target.latitude, geo_back->latitude, 0.01);
        EXPECT_NEAR(target.longitude, geo_back->longitude, 0.01);
    }
}
```

**This test FAILED before the Y-flip bug fix!**

---

### Testing Best Practices

1. **Test Symmetry:** Every transformation should have a reverse test
   ```cpp
   Geographic orig = ...;
   World world = GeographicToWorld(orig);
   Geographic back = WorldToGeographic(world);
   EXPECT_NEAR(orig.latitude, back.latitude, TOLERANCE);
   ```

2. **Test Boundary Conditions:**
   - Poles (lat = ±90°)
   - Date line (lon = ±180°)
   - Screen edges (x=0, y=0, x=width, y=height)

3. **Test Edge Cases:**
   - Ray misses sphere (returns `std::nullopt`)
   - Point behind camera (returns `std::nullopt`)
   - Out of bounds (Web Mercator latitude limits)

4. **Use Realistic Values:**
   - Don't just test (0,0,0) - tests real-world locations
   - Test multiple zoom levels for tiles
   - Test different viewport sizes

5. **Document Test Limitations:**
   ```cpp
   // NOTE: This test places target at screen center,
   // which is invariant under Y-flip. See OffCenter test for Y-flip detection.
   ```

---

## References

### External Standards

1. **Web Mercator Projection (EPSG:3857)**
   - [OpenStreetMap Wiki - Web Mercator](https://wiki.openstreetmap.org/wiki/Mercator)
   - Used by Google Maps, Bing Maps, OpenStreetMap, Mapbox

2. **Slippy Map Tile Names**
   - [OSM Wiki - Slippy Map Tilenames](https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames)
   - Standard tile numbering scheme (zoom/x/y)

3. **WGS84 Ellipsoid**
   - [EPSG:4326](https://epsg.io/4326)
   - World Geodetic System 1984
   - Semi-major axis: 6,378,137 meters
   - Flattening: 1/298.257223563

4. **OpenGL Coordinate Systems**
   - [OpenGL Transformation Pipeline](https://www.khronos.org/opengl/wiki/Vertex_Transformation)
   - NDC, Clip Space, Screen Space definitions

### Mathematical Foundations

1. **Spherical Coordinates**
   - [Spherical to Cartesian Conversion](https://mathworld.wolfram.com/SphericalCoordinates.html)
   - Convention: ISO 80000-2:2019 (physics convention)

2. **Ray-Sphere Intersection**
   - [Scratchapixel - Ray-Sphere Intersection](https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection)
   - Quadratic equation solution method

3. **Homogeneous Coordinates**
   - [Perspective Division](https://www.songho.ca/math/homogeneous/homogeneous.html)
   - 4D representation for 3D transformations

### Internal Documentation

- **CLAUDE.md** - Development guidelines and code style
- **PHASES_2_3_COMPLETE.md** - Implementation history
- **tests/unit/test_coordinate_mapper.cpp** - Comprehensive test documentation

---

## Appendix: Quick Reference

### Coordinate Space Cheat Sheet

| Space | Type | Range | Origin | Y-Axis |
|-------|------|-------|--------|--------|
| Geographic | `(lat, lon, alt)` | lat: [-90, 90], lon: [-180, 180] | Earth center | North positive |
| World | `(x, y, z)` | Unbounded (meters) | Earth center | North Pole (+Y) |
| Projected | `(x, y)` | ±20M meters | Equator/Prime Meridian | North positive |
| Tile | `(x, y, z)` | x,y: [0, 2^z-1], z: [0, 20] | NW corner | South positive |
| Screen | `(x, y)` | [0, viewport_size] | Bottom-left | Top positive (OpenGL) |

### Transformation Matrix

|           | Geographic | World | Projected | Tile | Screen |
|-----------|-----------|-------|-----------|------|--------|
| **Geographic** | - | Spherical→Cart | WebMercator | Proj+Normalize | Geo→World→Screen |
| **World** | Cart→Spherical | - | - | - | View+Proj+Viewport |
| **Projected** | WebMercator⁻¹ | - | - | Normalize+Scale | - |
| **Tile** | Denormalize+Unproj | - | - | - | - |
| **Screen** | Ray+Intersect+Cart→Sph | Ray+Intersect | - | - | - |

### Common Formulas

**Screen → NDC:**
```cpp
ndc.x = 2.0 * (screen.x / viewport_width) - 1.0
ndc.y = 2.0 * (screen.y / viewport_height) - 1.0
```

**NDC → Screen:**
```cpp
screen.x = (ndc.x * 0.5 + 0.5) * viewport_width
screen.y = (ndc.y * 0.5 + 0.5) * viewport_height
```

**Spherical → Cartesian:**
```cpp
x = r * cos(lat) * sin(lon)
y = r * sin(lat)
z = r * cos(lat) * cos(lon)
```

**Cartesian → Spherical:**
```cpp
lat = asin(y / r)
lon = atan2(x, z)
```

---

**End of Document**
